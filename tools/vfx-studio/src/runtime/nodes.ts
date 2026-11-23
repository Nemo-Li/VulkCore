import * as THREE from 'three';
import {
  animated,
  evaluateCurve,
  hashString,
  type EffectDocument,
  type Layer,
  type ShaderAsset,
} from '../core/model';
import { RuntimeAssets } from './assets';
import { generateParticles, particleGeometry, particleState, type ParticleData } from './particles';
import {
  cleanGLSL,
  fragmentDistortion,
  fragmentPost,
  fragmentVfx,
  vertexMesh,
  vertexParticle,
  vertexPost,
} from './shaders';

export const STYLE_INDEX: Record<Layer['material']['style'], number> = {
  glow: 0,
  smoke: 1,
  fire: 2,
  ring: 3,
  spark: 4,
  rune: 5,
  electric: 6,
  shield: 7,
};
export function defaultUniforms(white: THREE.Texture): Record<string, THREE.IUniform> {
  return {
    uTime: { value: 0 },
    uEffectTime: { value: 0 },
    uProgress: { value: 0 },
    uIntensity: { value: 1 },
    uOpacity: { value: 1 },
    uTint: { value: new THREE.Color('#71dcfa') },
    uSecondary: { value: new THREE.Color('#2960cf') },
    uSpeed: { value: 1 },
    uGravity: { value: 0 },
    uDrag: { value: 0 },
    uTurbulence: { value: 0 },
    uSize: { value: 1 },
    uStretch: { value: 1 },
    uRotationSpeed: { value: 0 },
    uEmitterMode: { value: 0 },
    uCycle: { value: 1 },
    uEmissionWindow: { value: 10 },
    uCameraRight: { value: new THREE.Vector3(1, 0, 0) },
    uCameraUp: { value: new THREE.Vector3(0, 1, 0) },
    uWorldScale: { value: 1 },
    uLifeCurve: { value: white },
    uStyle: { value: 0 },
    uIsRibbon: { value: 0 },
    uSoftness: { value: 0 },
    uDepthEnabled: { value: 0 },
    uTexture: { value: white },
    uHasTexture: { value: 0 },
    uSceneDepth: { value: white },
    uResolution: { value: new THREE.Vector2(1, 1) },
    uNearFar: { value: new THREE.Vector2(0.05, 150) },
    tDiffuse: { value: white },
    uSceneColor: { value: white },
    uEffectMask: { value: white },
    uStrength: { value: 0 },
    uSeed: { value: 0 },
  };
}
export function applyCustomUniforms(
  uniforms: Record<string, THREE.IUniform>,
  shader: ShaderAsset | undefined,
  layer: Layer | undefined,
  assets: RuntimeAssets,
  time?: number,
) {
  if (!shader) return;
  for (const def of shader.uniforms) {
    const raw = layer?.material.uniforms[def.name] ?? def.default;
    const v =
      time !== undefined && def.type === 'float'
        ? evaluateCurve(layer?.curves['uniforms.' + def.name], time, Number(raw))
        : raw;
    let converted: unknown = v;
    if (def.type === 'color') converted = new THREE.Color(String(v));
    if (def.type === 'texture') converted = assets.textures.get(String(v)) ?? assets.white;
    if (def.type === 'vec2') converted = new THREE.Vector2().fromArray(v as number[]);
    if (def.type === 'vec3') converted = new THREE.Vector3().fromArray(v as number[]);
    if (def.type === 'vec4') converted = new THREE.Vector4().fromArray(v as number[]);
    if (uniforms[def.name]) uniforms[def.name].value = converted;
    else uniforms[def.name] = { value: converted };
  }
}
export function shaderMaterial(
  shader: ShaderAsset | undefined,
  profile: 'particle' | 'mesh' | 'post',
  assets: RuntimeAssets,
  layer?: Layer,
): THREE.ShaderMaterial {
  const uniforms = defaultUniforms(assets.white);
  applyCustomUniforms(uniforms, shader, layer, assets);
  return new THREE.ShaderMaterial({
    name: shader?.name ?? `Ignis ${profile}`,
    glslVersion: THREE.GLSL3,
    vertexShader: cleanGLSL(
      shader?.vertex ||
        (profile === 'particle' ? vertexParticle : profile === 'post' ? vertexPost : vertexMesh),
    ),
    fragmentShader: cleanGLSL(
      shader?.fragment || (profile === 'post' ? fragmentPost : fragmentVfx),
    ),
    uniforms,
    transparent: true,
    depthWrite: false,
    side: THREE.DoubleSide,
    toneMapped: false,
  });
}
function meshGeometry(layer: Layer, assets: RuntimeAssets): THREE.BufferGeometry {
  const m = layer.mesh;
  let g: THREE.BufferGeometry;
  switch (m.shape) {
    case 'sphere':
      g = new THREE.SphereGeometry(1, 40, 28);
      break;
    case 'plane':
      g = new THREE.PlaneGeometry(1, 1, 1, 1);
      break;
    case 'ring':
      g = new THREE.RingGeometry(0.82, 1, 80);
      break;
    case 'torus':
      g = new THREE.TorusGeometry(1, Math.min(0.8, m.thickness / Math.max(0.01, m.radius)), 10, 80);
      break;
    case 'cone':
      g = new THREE.ConeGeometry(1, 1, 40, 1, true);
      break;
    case 'cylinder':
      g = new THREE.CylinderGeometry(1, 1, 1, 40, 1, true);
      break;
    case 'model':
      g = assets.models.get(m.modelId)?.clone() ?? new THREE.SphereGeometry(1, 16, 12);
      break;
  }
  if (m.orientation === 'horizontal' && ['plane', 'ring', 'torus'].includes(m.shape))
    g.rotateX(-Math.PI / 2);
  return g;
}
export class LayerNode {
  group = new THREE.Group();
  fieldGroup = new THREE.Group();
  mesh?: THREE.Mesh<THREE.BufferGeometry, THREE.ShaderMaterial>;
  field?: THREE.Mesh<THREE.BufferGeometry, THREE.ShaderMaterial>;
  light?: THREE.PointLight;
  layer: Layer;
  private assets: RuntimeAssets;
  private geometrySignature = '';
  private shaderSignature = '';
  private lifeSignature = '';
  private seed = 0;
  particleData?: ParticleData;
  private lifeTexture?: THREE.DataTexture;
  private disposed = false;
  private activeShader?: ShaderAsset;
  constructor(layer: Layer, doc: EffectDocument, assets: RuntimeAssets) {
    this.layer = layer;
    this.assets = assets;
    this.group.name = layer.name;
    this.group.userData.layerId = layer.id;
    this.patch(layer, doc);
  }
  patch(layer: Layer, doc: EffectDocument) {
    this.layer = layer;
    this.seed = doc.seed;
    this.group.name = layer.name;
    const profile = layer.kind === 'particles' ? 'particle' : 'mesh';
    const shader = doc.shaders.find((s) => s.id === layer.material.shaderId);
    this.activeShader = shader;
    const shaderSig = profile + '|' + JSON.stringify(shader ?? null);
    if (['particles', 'mesh', 'ribbon'].includes(layer.kind)) {
      if (!this.mesh) {
        this.mesh = new THREE.Mesh(
          new THREE.BufferGeometry(),
          shaderMaterial(shader, profile, this.assets, layer),
        );
        this.mesh.frustumCulled = false;
        this.group.add(this.mesh);
      }
      if (this.shaderSignature !== shaderSig) {
        const old = this.mesh.material;
        this.mesh.material = shaderMaterial(shader, profile, this.assets, layer);
        old.dispose();
        this.shaderSignature = shaderSig;
      }
      const sig =
        layer.kind === 'particles'
          ? JSON.stringify(layer.emitter) + doc.seed
          : layer.kind === 'mesh'
            ? JSON.stringify([
                layer.mesh.shape,
                layer.mesh.orientation,
                layer.mesh.modelId,
                layer.mesh.thickness,
                layer.mesh.radius,
                this.assets.models.get(layer.mesh.modelId)?.uuid,
              ])
            : `ribbon${layer.ribbon.segments}`;
      if (this.geometrySignature !== sig) {
        this.mesh.geometry.dispose();
        if (layer.kind === 'particles') {
          this.particleData = generateParticles(layer, doc.seed);
          this.mesh.geometry = particleGeometry(this.particleData);
        } else if (layer.kind === 'mesh') this.mesh.geometry = meshGeometry(layer, this.assets);
        else {
          const n = (layer.ribbon.segments + 1) * 2;
          const g = new THREE.BufferGeometry();
          g.setAttribute('position', new THREE.Float32BufferAttribute(new Float32Array(n * 3), 3));
          g.setAttribute(
            'normal',
            new THREE.Float32BufferAttribute(
              new Float32Array(n * 3).fill(0).map((_, i) => (i % 3 === 1 ? 1 : 0)),
              3,
            ),
          );
          const uv = new Float32Array(n * 2),
            idx: number[] = [];
          for (let i = 0; i <= layer.ribbon.segments; i++) {
            uv.set([i / layer.ribbon.segments, 0, i / layer.ribbon.segments, 1], i * 4);
            if (i < layer.ribbon.segments) {
              const k = i * 2;
              idx.push(k, k + 1, k + 2, k + 1, k + 3, k + 2);
            }
          }
          g.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
          g.setIndex(idx);
          this.mesh.geometry = g;
        }
        this.geometrySignature = sig;
      }
      const lifeSig = JSON.stringify([layer.curves['life.size'], layer.curves['life.opacity']]);
      if (layer.kind === 'particles' && this.lifeSignature !== lifeSig) {
        this.lifeTexture?.dispose();
        const data = new Float32Array(128 * 4);
        for (let i = 0; i < 128; i++) {
          const t = i / 127;
          data[i * 4] = Math.max(
            0,
            evaluateCurve(layer.curves['life.size'], t, Math.sin(Math.PI * t) ** 0.3),
          );
          data[i * 4 + 1] = Math.max(
            0,
            evaluateCurve(
              layer.curves['life.opacity'],
              t,
              Math.min(t * 12, 1) * Math.max(0, 1 - t) ** 0.6,
            ),
          );
          data[i * 4 + 2] = 1;
          data[i * 4 + 3] = 1;
        }
        this.lifeTexture = new THREE.DataTexture(data, 128, 1, THREE.RGBAFormat, THREE.FloatType);
        this.lifeTexture.minFilter = this.lifeTexture.magFilter = THREE.LinearFilter;
        this.lifeTexture.needsUpdate = true;
        this.lifeSignature = lifeSig;
      }
      const mat = this.mesh.material;
      mat.blending =
        layer.material.blending === 'add' ? THREE.AdditiveBlending : THREE.NormalBlending;
      mat.depthWrite = layer.material.depthWrite;
      mat.wireframe = layer.mesh.wireframe;
      applyCustomUniforms(mat.uniforms, shader, layer, this.assets);
    }
    if (layer.kind === 'light' && !this.light) {
      this.light = new THREE.PointLight(
        layer.material.color,
        layer.light.intensity,
        layer.light.distance,
        layer.light.decay,
      );
      this.group.add(this.light);
    }
    if (this.field && !['distortion', 'custom'].includes(layer.post.mode)) {
      this.field.geometry.dispose();
      this.field.material.dispose();
      this.field.removeFromParent();
      this.field = undefined;
    }
    if (
      layer.kind === 'postprocess' &&
      ['distortion', 'custom'].includes(layer.post.mode) &&
      !this.field
    ) {
      const m = shaderMaterial(undefined, 'mesh', this.assets);
      m.vertexShader = vertexMesh;
      m.fragmentShader = fragmentDistortion;
      m.blending = THREE.CustomBlending;
      m.blendSrc = THREE.OneFactor;
      m.blendDst = THREE.OneFactor;
      m.blendEquation = THREE.AddEquation;
      m.depthTest = false;
      this.field = new THREE.Mesh(new THREE.PlaneGeometry(2, 2), m);
      this.field.frustumCulled = false;
      this.fieldGroup.add(this.field);
    }
  }
  update(
    time: number,
    camera: THREE.PerspectiveCamera,
    context: {
      depth: THREE.Texture;
      width: number;
      height: number;
      depthEnabled: boolean;
      intensity: number;
      stopTime: number | null;
    },
    index: number,
  ) {
    const l = this.layer,
      t = time - l.start,
      active =
        l.enabled &&
        t >= 0 &&
        t <= l.duration &&
        (context.stopTime === null || l.start <= context.stopTime);
    this.group.visible = active;
    this.fieldGroup.visible = active && context.stopTime === null;
    if (!this.group.userData.transforming) {
      this.group.position.fromArray(l.transform.position);
      this.group.rotation.set(...l.transform.rotation);
      this.group.scale.fromArray(l.transform.scale);
      for (const axis of [0, 1, 2]) {
        this.group.position.setComponent(axis, animated(l, `transform.position.${axis}`, t));
        this.group.scale.setComponent(axis, animated(l, `transform.scale.${axis}`, t));
      }
      this.group.rotation.set(
        animated(l, 'transform.rotation.0', t),
        animated(l, 'transform.rotation.1', t),
        animated(l, 'transform.rotation.2', t),
      );
    }
    this.group.updateMatrixWorld(true);
    this.fieldGroup.position.copy(this.group.position);
    this.fieldGroup.quaternion.copy(this.group.quaternion);
    this.fieldGroup.scale.copy(this.group.scale);
    if (this.light) {
      this.light.visible = context.stopTime === null;
      this.light.color.set(l.material.color);
      this.light.intensity = animated(l, 'light.intensity', t) * context.intensity;
      this.light.distance =
        (l.light.distance * this.group.getWorldScale(new THREE.Vector3()).length()) / Math.sqrt(3);
      this.light.decay = l.light.decay;
    }
    if (this.mesh) {
      this.mesh.renderOrder = index;
      const u = this.mesh.material.uniforms;
      applyCustomUniforms(u, this.activeShader, l, this.assets, t);
      u.uTime.value = t;
      u.uEffectTime.value = time;
      u.uProgress.value = Math.max(0, Math.min(1, t / l.duration));
      u.uSeed.value = this.seed;
      u.uTint.value.set(l.material.color);
      u.uSecondary.value.set(l.material.secondaryColor);
      u.uIntensity.value = animated(l, 'material.intensity', t) * context.intensity;
      u.uOpacity.value = animated(l, 'material.opacity', t);
      u.uStyle.value = STYLE_INDEX[l.material.style];
      u.uIsRibbon.value = l.kind === 'ribbon' ? 1 : 0;
      u.uSoftness.value = l.material.softness;
      u.uSceneDepth.value = context.depth;
      u.uResolution.value.set(context.width, context.height);
      u.uNearFar.value.set(camera.near, camera.far);
      u.uDepthEnabled.value = context.depthEnabled ? 1 : 0;
      u.uCameraRight.value.setFromMatrixColumn(camera.matrixWorld, 0);
      u.uCameraUp.value.setFromMatrixColumn(camera.matrixWorld, 1);
      u.uWorldScale.value = this.group.getWorldScale(new THREE.Vector3()).length() / Math.sqrt(3);
      const texture = this.assets.textures.get(l.material.textureId);
      u.uHasTexture.value = texture ? 1 : 0;
      u.uTexture.value = texture ?? this.assets.white;
      if (l.kind === 'particles') {
        const e = l.emitter;
        for (const [key, path] of Object.entries({
          uSpeed: 'speed',
          uGravity: 'gravity',
          uDrag: 'drag',
          uTurbulence: 'turbulence',
          uSize: 'size',
          uStretch: 'stretch',
          uRotationSpeed: 'rotationSpeed',
        }))
          u[key].value = animated(l, `emitter.${path}`, t);
        u.uEmitterMode.value = e.mode === 'continuous' ? 1 : 0;
        u.uCycle.value = e.count / e.rate;
        u.uEmissionWindow.value =
          context.stopTime === null
            ? e.emissionWindow
            : Math.min(e.emissionWindow, context.stopTime - l.start);
        u.uLifeCurve.value = this.lifeTexture ?? this.assets.white;
      } else if (l.kind === 'mesh') {
        const m = l.mesh,
          r = animated(l, 'mesh.radius', t);
        if (m.shape === 'plane') {
          const w = animated(l, 'mesh.width', t),
            h = animated(l, 'mesh.height', t);
          this.mesh.scale.set(
            w,
            m.orientation === 'horizontal' ? 1 : h,
            m.orientation === 'horizontal' ? h : 1,
          );
        } else
          this.mesh.scale.set(
            r,
            ['cone', 'cylinder'].includes(m.shape) ? animated(l, 'mesh.height', t) : r,
            r,
          );
      } else this.updateRibbon(t, camera);
    }
    if (this.field) {
      const strength = animated(l, 'post.strength', t) * context.intensity;
      const u = this.field.material.uniforms;
      u.uTime.value = t;
      u.uProgress.value = Math.max(0, Math.min(1, t / l.duration));
      u.uStrength.value = l.post.mode === 'distortion' ? strength : 0;
      u.uSceneDepth.value = context.depth;
      u.uResolution.value.set(context.width, context.height);
      u.uNearFar.value.set(camera.near, camera.far);
      u.uDepthEnabled.value = context.depthEnabled ? 1 : 0;
      this.field.scale.setScalar(animated(l, 'post.radius', t));
      this.fieldGroup.updateWorldMatrix(true, false);
      this.field.quaternion.copy(
        this.fieldGroup
          .getWorldQuaternion(new THREE.Quaternion())
          .invert()
          .multiply(camera.quaternion),
      );
    }
  }
  private updateRibbon(time: number, camera: THREE.PerspectiveCamera) {
    if (!this.mesh) return;
    const l = this.layer,
      r = l.ribbon,
      n = r.segments,
      positions = this.mesh.geometry.getAttribute('position') as THREE.BufferAttribute;
    const cam = this.group.worldToLocal(camera.getWorldPosition(new THREE.Vector3()));
    const width = animated(l, 'ribbon.width', time),
      radius = animated(l, 'ribbon.radius', time);
    const points: THREE.Vector3[] = [];
    for (let i = 0; i <= n; i++) {
      const u = i / n;
      let f = time * r.speed * 0.35 - (1 - u) * r.tail;
      if (r.path === 'bolt') f = u;
      const a = f * Math.PI * 2 * r.turns;
      let x = Math.cos(a) * radius,
        z = Math.sin(a) * radius,
        y = f * r.height;
      if (r.path === 'orbit') {
        y = Math.sin(a * 0.7) * r.height * 0.4;
      }
      if (r.path === 'arc') {
        const t = Math.max(0, Math.min(1, f));
        x = (t - 0.5) * radius * 2;
        z = Math.sin(t * Math.PI) * radius * 0.35;
        y = Math.sin(t * Math.PI) * r.height;
      }
      if (r.path === 'bolt') {
        const flicker = Math.floor(time * 18);
        x = Math.sin(i * 7.7 + flicker + hashString(l.id)) * r.jitter * Math.sin(u * Math.PI);
        z = Math.cos(i * 11.3 + flicker) * r.jitter * Math.sin(u * Math.PI);
        y = u * r.height;
      }
      points.push(new THREE.Vector3(x, y, z));
    }
    for (let i = 0; i <= n; i++) {
      const p = points[i],
        tangent = points[Math.min(n, i + 1)].clone().sub(points[Math.max(0, i - 1)]);
      const side = tangent
        .cross(cam.clone().sub(p))
        .normalize()
        .multiplyScalar(width * 0.5);
      positions.setXYZ(i * 2, p.x - side.x, p.y - side.y, p.z - side.z);
      positions.setXYZ(i * 2 + 1, p.x + side.x, p.y + side.y, p.z + side.z);
    }
    positions.needsUpdate = true;
    this.mesh.geometry.computeBoundingSphere();
  }
  aliveCount(time: number, stopTime: number | null = null) {
    if (!this.group.visible || !this.particleData) return 0;
    let n = 0;
    for (let i = 0; i < this.particleData.starts.length; i++) {
      const p = particleState(this.particleData, i, this.layer, time - this.layer.start);
      if (p.alive && (stopTime === null || p.birth <= stopTime - this.layer.start)) n++;
    }
    return n;
  }
  dispose() {
    if (this.disposed) return;
    this.disposed = true;
    this.mesh?.geometry.dispose();
    this.mesh?.material.dispose();
    this.field?.geometry.dispose();
    this.field?.material.dispose();
    this.lifeTexture?.dispose();
    this.group.removeFromParent();
    this.fieldGroup.removeFromParent();
  }
}
