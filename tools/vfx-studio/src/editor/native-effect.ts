import * as THREE from 'three';
import { RuntimeAssets } from '../runtime/assets';
import { LayerNode, STYLE_INDEX } from '../runtime/nodes';
import { fragmentVfx, vertexMesh, vertexParticle, vertexPost } from '../runtime/shaders';
import {
  evaluateCurve,
  getPath,
  hashString,
  parseDocument,
  type EffectDocument,
} from '../core/model';
import { CUSTOM_TEXTURES, PARAMETER_COUNT, customSlots, nativeShader } from './native-shader';
import { nativePostFragment } from './native-post';

// Stored in this order by the v1 file format.
const scalarPaths = [
  ...['position', 'rotation', 'scale'].flatMap((p) => [0, 1, 2].map((i) => `transform.${p}.${i}`)),
  'material.intensity',
  'material.opacity',
  ...['speed', 'gravity', 'drag', 'turbulence', 'size', 'stretch', 'rotationSpeed'].map(
    (p) => 'emitter.' + p,
  ),
  'mesh.radius',
  'mesh.width',
  'mesh.height',
  'ribbon.width',
  'ribbon.radius',
  'light.intensity',
  'post.strength',
  'post.radius',
];

class Writer {
  private parts: ArrayBuffer[] = [];
  private size = 0;
  private buffer = new ArrayBuffer(64 * 1024);
  private view = new DataView(this.buffer);
  private offset = 0;

  private flush() {
    if (!this.offset) return;
    this.append(this.buffer.slice(0, this.offset));
    this.offset = 0;
  }
  private append(bytes: ArrayBuffer) {
    this.size += bytes.byteLength;
    if (this.size > 128 * 1024 * 1024) throw new Error('Vulkan 特效包超过 128 MiB');
    this.parts.push(bytes);
  }
  uint(value: number) {
    if (this.offset === this.buffer.byteLength) this.flush();
    this.view.setUint32(this.offset, value, true);
    this.offset += 4;
  }
  float(value: number) {
    if (this.offset === this.buffer.byteLength) this.flush();
    this.view.setFloat32(this.offset, value, true);
    this.offset += 4;
  }
  floats(values: Iterable<number>) {
    for (const value of values) this.float(value);
  }
  bytes(bytes: Uint8Array) {
    this.uint(bytes.length);
    this.flush();
    this.append(Uint8Array.from(bytes).buffer);
  }
  string(value: string) {
    this.bytes(new TextEncoder().encode(value));
  }
  blob() {
    this.flush();
    return new Blob(this.parts, { type: 'application/octet-stream' });
  }
}

function loadCompiler() {
  // This distribution embeds WASM bytes, which also works in the WKWebView host.
  return import('@webgpu/glslang/dist/web-devel-onefile/glslang.js').then((x) => x.default());
}

async function encodeTexture(image: HTMLImageElement) {
  const canvas = document.createElement('canvas');
  canvas.width = image.width;
  canvas.height = image.height;
  const ctx = canvas.getContext('2d');
  if (!ctx) throw new Error('无法读取贴图像素');
  // Match TextureLoader's vertically flipped upload.
  ctx.translate(0, canvas.height);
  ctx.scale(1, -1);
  ctx.drawImage(image, 0, 0);
  const png = await new Promise<Blob>((resolve, reject) =>
    canvas.toBlob(
      (blob) => (blob ? resolve(blob) : reject(new Error('无法编码贴图'))),
      'image/png',
    ),
  );
  return new Uint8Array(await png.arrayBuffer());
}

type PreviewFraming = {
  position: [number, number, number];
  target: [number, number, number];
  fov: number;
  aspect: number;
};

export async function exportNativeEffect(
  input: EffectDocument,
  framing: PreviewFraming,
  assertCurrent = () => {},
) {
  const doc = parseDocument(input);
  const cameraValues = [...framing.position, ...framing.target, framing.fov, framing.aspect];
  const distance = new THREE.Vector3(...framing.position).distanceTo(
    new THREE.Vector3(...framing.target),
  );
  if (
    !cameraValues.every(Number.isFinite) ||
    framing.fov <= 0 ||
    framing.fov >= 179 ||
    framing.aspect < 0.01 ||
    framing.aspect > 100 ||
    distance < 0.001 ||
    distance > 10000
  ) {
    throw new Error('特效视口尚未准备好，请重置视角后重新导出');
  }
  const compiler = await loadCompiler();
  const assets = new RuntimeAssets();
  try {
    await assets.sync(doc);
    assertCurrent();
    const layers = doc.layers.filter((layer) => layer.enabled);
    if (!layers.length) throw new Error('至少启用一个特效层');
    const shaderFor = (layer: EffectDocument['layers'][number]) =>
      doc.shaders.find(
        (s) =>
          s.id === (layer.kind === 'postprocess' ? layer.post.shaderId : layer.material.shaderId),
      );
    const textureIds = new Set<string>();
    for (const layer of layers) {
      if (layer.material.textureId) textureIds.add(layer.material.textureId);
      for (const uniform of customSlots(shaderFor(layer))) {
        if (uniform.type !== 'texture') continue;
        const id = String(layer.material.uniforms[uniform.name] ?? uniform.default);
        if (id) textureIds.add(id);
      }
    }
    const ids = [...textureIds];
    const writer = new Writer();
    writer.string('VulkCoreRealtimeEffect');
    writer.uint(2);
    writer.string(doc.name);
    writer.float(doc.duration);
    writer.uint(Number(doc.loop));
    writer.float(doc.seed);
    writer.uint(Number(doc.settings.bloom));
    writer.floats([
      doc.settings.bloomStrength,
      doc.settings.bloomThreshold,
      doc.settings.bloomRadius,
    ]);
    writer.floats(cameraValues);
    writer.uint(ids.length);
    const textureIndex = (id: string) => {
      const index = ids.indexOf(id);
      if (id && index < 0) throw new Error('贴图引用不存在');
      return index + 1; // Zero selects the white texture.
    };
    for (const id of ids) {
      const texture = assets.textures.get(id);
      if (!texture) throw new Error('贴图加载失败');
      writer.uint(Number(texture.colorSpace === THREE.SRGBColorSpace));
      writer.bytes(await encodeTexture(texture.image as HTMLImageElement));
      assertCurrent();
    }
    writer.uint(layers.length);
    const shaderCache = new Map<string, Uint32Array>();
    const compile = (source: string, stage: 'vertex' | 'fragment') => {
      const key = stage + source;
      let code = shaderCache.get(key);
      if (!code) {
        code = compiler.compileGLSL(source, stage, false, '1.0');
        if (!code.length) throw new Error('着色器编译未产生 SPIR-V');
        shaderCache.set(key, code);
      }
      writer.bytes(new Uint8Array(code.buffer, code.byteOffset, code.byteLength));
    };
    for (const layer of layers) {
      const post = layer.kind === 'postprocess';
      const shader = shaderFor(layer);
      try {
        const kind = ['particles', 'mesh', 'ribbon', 'light', 'postprocess'].indexOf(layer.kind);
        writer.uint(kind);
        writer.uint(
          (layer.material.blending === 'add' ? 1 : 0) |
            (layer.material.depthWrite ? 2 : 0) |
            (layer.mesh.wireframe ? 4 : 0) |
            (post && layer.post.stage === 'afterBloom' && layer.post.mode === 'custom' ? 8 : 0) |
            (post
              ? layer.post.mode === 'distortion' || layer.post.mode === 'custom'
                ? 16
                : 0
              : layer.material.softness > 0 || shader
                ? 16
                : 0),
        );
        writer.floats([
          layer.start,
          layer.duration,
          layer.material.softness,
          STYLE_INDEX[layer.material.style],
          Number(layer.emitter.mode === 'continuous'),
          layer.emitter.count / layer.emitter.rate,
          layer.emitter.emissionWindow,
        ]);
        writer.floats(new THREE.Color(layer.material.color).toArray());
        writer.floats(new THREE.Color(layer.material.secondaryColor).toArray());
        writer.uint(
          ['sphere', 'plane', 'ring', 'torus', 'cone', 'cylinder', 'model'].indexOf(
            layer.mesh.shape,
          ),
        );
        writer.uint(Number(layer.mesh.orientation === 'horizontal'));
        writer.uint(['spiral', 'orbit', 'arc', 'bolt'].indexOf(layer.ribbon.path));
        writer.uint(layer.ribbon.segments);
        writer.floats([
          layer.ribbon.height,
          layer.ribbon.turns,
          layer.ribbon.speed,
          layer.ribbon.tail,
          layer.ribbon.jitter,
          hashString(layer.id) % (Math.PI * 2),
          layer.light.distance,
          layer.light.decay,
        ]);
        writer.uint(textureIndex(layer.material.textureId));
        const textures = Array<number>(CUSTOM_TEXTURES).fill(0);
        const custom = new Float32Array(PARAMETER_COUNT);
        const tracks = scalarPaths.map((path, slot) => ({
          path,
          slot,
          base: Number(getPath(layer, path)),
        }));
        for (const uniform of customSlots(shader)) {
          const value = layer.material.uniforms[uniform.name] ?? uniform.default;
          if (uniform.type === 'texture') textures[uniform.slot] = textureIndex(String(value));
          else if (uniform.type === 'float')
            tracks.push({
              path: 'uniforms.' + uniform.name,
              base: Number(value),
              slot: uniform.slot + scalarPaths.length,
            });
          else if (uniform.type === 'color')
            custom.set(new THREE.Color(String(value)).toArray(), uniform.slot);
          else if (Array.isArray(value)) custom.set(value, uniform.slot);
          else custom[uniform.slot] = Number(value);
        }
        for (const index of textures) writer.uint(index);
        writer.floats(custom);
        writer.uint(tracks.length);
        for (const { path, base, slot } of tracks) {
          const curve = layer.curves[path];
          writer.uint(slot);
          writer.float(base);
          writer.uint(curve ? ['linear', 'step', 'smooth'].indexOf(curve.interpolation) : 0);
          writer.uint(curve?.keys.length ?? 0);
          for (const key of curve?.keys ?? []) writer.floats([key.time, key.value]);
        }
        if (layer.kind === 'light') continue;
        if (layer.kind === 'particles') {
          for (let i = 0; i < 128; i++) {
            const t = i / 127;
            writer.floats([
              Math.max(
                0,
                evaluateCurve(layer.curves['life.size'], t, Math.sin(Math.PI * t) ** 0.3),
              ),
              Math.max(
                0,
                evaluateCurve(
                  layer.curves['life.opacity'],
                  t,
                  Math.min(t * 12, 1) * (1 - t) ** 0.6,
                ),
              ),
            ]);
          }
        }
        const profile = layer.kind === 'particles' ? 'particle' : post ? 'post' : 'mesh';
        const useCustom = !post || layer.post.mode === 'custom';
        const [vs, fs] = nativeShader(
          (useCustom && shader?.vertex) ||
            (post ? vertexPost : profile === 'particle' ? vertexParticle : vertexMesh),
          (useCustom && shader?.fragment) ||
            (post ? nativePostFragment(layer.post.mode) : fragmentVfx),
          profile,
          shader,
        );
        compile(vs, 'vertex');
        compile(fs, 'fragment');
        const node = post ? undefined : new LayerNode(layer, doc, assets);
        const geometry = node?.mesh?.geometry ?? new THREE.PlaneGeometry(2, 2);
        try {
          const p = geometry.getAttribute('position'),
            n = geometry.getAttribute('normal'),
            uv = geometry.getAttribute('uv');
          writer.uint(p.count);
          for (let i = 0; i < p.count; ++i)
            writer.floats([
              p.getX(i),
              p.getY(i),
              p.getZ(i),
              n.getX(i),
              n.getY(i),
              n.getZ(i),
              uv.getX(i),
              uv.getY(i),
            ]);
          const indices = geometry.index?.array ?? Array.from({ length: p.count }, (_, i) => i);
          const wireframe = layer.mesh.wireframe && !post;
          writer.uint(wireframe ? indices.length * 2 : indices.length);
          if (wireframe) {
            for (let i = 0; i < indices.length; i += 3) {
              const a = indices[i],
                b = indices[i + 1],
                c = indices[i + 2];
              for (const v of [a, b, b, c, c, a]) writer.uint(v);
            }
          } else for (const index of indices) writer.uint(index);
          const data = node?.particleData;
          writer.uint(data?.starts.length ?? 0);
          if (data) {
            for (let i = 0; i < data.starts.length; i++) {
              writer.floats([
                ...data.positions.subarray(i * 3, i * 3 + 3),
                ...data.velocities.subarray(i * 3, i * 3 + 3),
                data.starts[i],
                data.lifetimes[i],
                data.sizes[i],
                data.rotations[i],
                data.randoms[i],
              ]);
            }
          }
        } finally {
          if (node) node.dispose();
          else geometry.dispose();
        }
      } catch (error) {
        throw new Error(`${layer.name}：${error instanceof Error ? error.message : String(error)}`);
      }
      await new Promise<void>((resolve) => setTimeout(resolve, 0));
      assertCurrent();
    }
    return { blob: writer.blob(), filename: doc.name + '.vfx3d' };
  } finally {
    assets.dispose();
  }
}
