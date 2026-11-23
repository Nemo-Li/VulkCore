import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { randomUUID } from '../core/crypto';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';
import { parseDocument, type EffectDocument, type Asset } from '../core/model';
import { validateStaticGlb } from '../core/glb';

function projectedUv(geometry: THREE.BufferGeometry) {
  geometry.computeBoundingBox();
  const box = geometry.boundingBox!,
    size = box.getSize(new THREE.Vector3()),
    p = geometry.getAttribute('position'),
    n = geometry.getAttribute('normal'),
    uv = new Float32Array(p.count * 2);
  const min = box.min.toArray(),
    extent = size.toArray();
  for (let i = 0; i < p.count; i++) {
    const normal = [Math.abs(n.getX(i)), Math.abs(n.getY(i)), Math.abs(n.getZ(i))],
      axis = normal.indexOf(Math.max(...normal)),
      pair = axis === 0 ? [2, 1] : axis === 1 ? [0, 2] : [0, 1],
      position = [p.getX(i), p.getY(i), p.getZ(i)];
    for (let j = 0; j < 2; j++)
      uv[i * 2 + j] = (position[pair[j]] - min[pair[j]]) / Math.max(extent[pair[j]], 0.00001);
  }
  geometry.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
}
function disposeGltf(root: THREE.Object3D) {
  root.traverse((o) => {
    if (!(o instanceof THREE.Mesh)) return;
    o.geometry.dispose();
    for (const m of Array.isArray(o.material) ? o.material : [o.material]) {
      for (const value of Object.values(m)) if (value instanceof THREE.Texture) value.dispose();
      m.dispose();
    }
  });
}
async function staticModel(url: string, name: string) {
  const response = await fetch(url);
  if (!response.ok) throw new Error(`${name}：模型加载失败 ${response.status}`);
  const bytes = await response.arrayBuffer();
  validateStaticGlb(bytes);
  const gltf = await new GLTFLoader().parseAsync(bytes, ''),
    parts: THREE.BufferGeometry[] = [];
  let vertices = 0;
  try {
    gltf.scene.updateMatrixWorld(true);
    gltf.scene.traverse((o) => {
      if (!(o instanceof THREE.Mesh)) return;
      const count = o.geometry.index?.count ?? o.geometry.getAttribute('position').count;
      if (vertices + count > 500000) throw new Error(`${name}：模型超过 50 万展开顶点`);
      let geometry = o.geometry.clone().applyMatrix4(o.matrixWorld);
      if (geometry.index) {
        const expanded = geometry.toNonIndexed();
        geometry.dispose();
        geometry = expanded;
      }
      for (const attribute of Object.keys(geometry.attributes))
        if (!['position', 'normal', 'uv'].includes(attribute)) geometry.deleteAttribute(attribute);
      geometry.morphAttributes = {};
      if (!geometry.getAttribute('normal')) geometry.computeVertexNormals();
      if (!geometry.getAttribute('uv')) projectedUv(geometry);
      vertices += geometry.getAttribute('position').count;
      parts.push(geometry);
    });
    if (!parts.length || vertices > 500000)
      throw new Error(`${name}：静态模型需包含网格且不超过 50 万展开顶点`);
    const merged = mergeGeometries(parts, false);
    if (!merged) throw new Error('模型几何合并失败');
    return merged;
  } finally {
    parts.forEach((g) => g.dispose());
    disposeGltf(gltf.scene);
  }
}
export class RuntimeAssets {
  readonly white = new THREE.DataTexture(new Uint8Array([255, 255, 255, 255]), 1, 1);
  constructor() {
    this.white.needsUpdate = true;
  }
  textures = new Map<string, THREE.Texture>();
  models = new Map<string, THREE.BufferGeometry>();
  private fingerprints = new Map<string, { src: string; colorSpace: string; type: string }>();
  async sync(
    doc: EffectDocument,
    baseUrl: string = globalThis.location?.href ?? 'http://127.0.0.1/',
  ) {
    const valid = new Set(doc.assets.map((a) => a.id));
    for (const [id, t] of this.textures)
      if (!valid.has(id)) {
        t.dispose();
        this.textures.delete(id);
        this.fingerprints.delete(id);
      }
    for (const [id, g] of this.models)
      if (!valid.has(id)) {
        g.dispose();
        this.models.delete(id);
        this.fingerprints.delete(id);
      }
    const results = await Promise.allSettled(
      doc.assets.map(async (asset) => {
        const old = this.fingerprints.get(asset.id);
        if (
          old?.src === asset.src &&
          old.colorSpace === asset.colorSpace &&
          old.type === asset.type
        )
          return;
        const url = new URL(asset.src, baseUrl).href;
        if (asset.type === 'texture') {
          const texture = await new THREE.TextureLoader().loadAsync(url),
            image = texture.image as HTMLImageElement;
          if (image.width > 8192 || image.height > 8192) {
            texture.dispose();
            throw new Error(asset.name + '：贴图尺寸不能超过 8192 × 8192');
          }
          texture.colorSpace =
            asset.colorSpace === 'srgb' ? THREE.SRGBColorSpace : THREE.NoColorSpace;
          texture.wrapS = texture.wrapT = THREE.ClampToEdgeWrapping;
          texture.anisotropy = 4;
          this.textures.get(asset.id)?.dispose();
          this.textures.set(asset.id, texture);
          this.models.get(asset.id)?.dispose();
          this.models.delete(asset.id);
        } else {
          const geometry = await staticModel(url, asset.name);
          this.models.get(asset.id)?.dispose();
          this.models.set(asset.id, geometry);
          this.textures.get(asset.id)?.dispose();
          this.textures.delete(asset.id);
        }
        this.fingerprints.set(asset.id, {
          src: asset.src,
          colorSpace: asset.colorSpace,
          type: asset.type,
        });
      }),
    );
    const errors = results.filter((r): r is PromiseRejectedResult => r.status === 'rejected');
    if (errors.length)
      throw new Error(
        errors
          .map((r) => (r.reason instanceof Error ? r.reason.message : String(r.reason)))
          .join('\n'),
      );
  }
  dispose() {
    this.white.dispose();
    for (const t of this.textures.values()) t.dispose();
    for (const g of this.models.values()) g.dispose();
    this.textures.clear();
    this.models.clear();
    this.fingerprints.clear();
  }
}
export async function loadEffectDocument(url: string): Promise<EffectDocument> {
  const absolute = new URL(url, globalThis.location?.href ?? 'http://127.0.0.1/').href,
    response = await fetch(absolute);
  if (!response.ok) throw new Error(`资源包请求失败 ${response.status}`);
  const doc = parseDocument(await response.json());
  await Promise.all(
    doc.shaders.map(async (shader) => {
      for (const key of ['vertex', 'fragment'] as const) {
        const path = shader[(key + 'Path') as 'vertexPath' | 'fragmentPath'];
        if (path) {
          const res = await fetch(new URL(path, absolute));
          if (!res.ok) throw new Error(`Shader 加载失败 ${path}`);
          shader[key] = await res.text();
        }
      }
    }),
  );
  for (const asset of doc.assets) asset.src = new URL(asset.src, absolute).href;
  return parseDocument(doc);
}
export async function fileAsset(file: File): Promise<Asset> {
  if (file.size > 64 * 1024 * 1024) throw new Error('单个资源最大 64 MiB');
  const type = file.name.toLowerCase().endsWith('.glb') ? 'model' : 'texture';
  if (
    type === 'texture' &&
    !['image/png', 'image/jpeg', 'image/webp'].includes(file.type) &&
    !/\.(png|jpe?g|webp)$/i.test(file.name)
  )
    throw new Error('贴图支持 PNG、JPEG、WebP；模型支持 GLB');
  const src = await new Promise<string>((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(String(reader.result));
    reader.onerror = () => reject(reader.error);
    reader.readAsDataURL(file);
  });
  return {
    id: 'asset_' + randomUUID().slice(0, 8),
    name: file.name,
    type,
    src,
    colorSpace: type === 'texture' ? 'srgb' : 'linear',
    bytes: file.size,
  };
}
