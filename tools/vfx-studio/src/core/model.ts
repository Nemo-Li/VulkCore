import { z } from 'zod';
import { randomUUID } from './crypto';

const finite = z.number().finite();
const vec3 = z.tuple([
  finite.min(-100000).max(100000),
  finite.min(-100000).max(100000),
  finite.min(-100000).max(100000),
]);
const color = z.string().regex(/^#[0-9a-fA-F]{6}$/);
const value = z.union([finite, z.string().max(100), z.boolean(), z.array(finite).min(2).max(4)]);
export const AUTO_UNIFORMS = new Set([
  'uTime',
  'uEffectTime',
  'uProgress',
  'uIntensity',
  'uOpacity',
  'uTint',
  'uSecondary',
  'uResolution',
  'uNearFar',
  'uSceneDepth',
  'uSceneColor',
  'uEffectMask',
  'tDiffuse',
  'uStrength',
  'uSeed',
  'uTexture',
  'uHasTexture',
  'uCameraRight',
  'uCameraUp',
  'uWorldScale',
  'uLifeCurve',
  'uSpeed',
  'uGravity',
  'uDrag',
  'uTurbulence',
  'uSize',
  'uStretch',
  'uRotationSpeed',
  'uEmitterMode',
  'uCycle',
  'uEmissionWindow',
  'uStyle',
  'uIsRibbon',
  'uSoftness',
  'uDepthEnabled',
]);
export const ANIMATABLE_PATHS = new Set([
  'material.opacity',
  'material.intensity',
  'emitter.speed',
  'emitter.gravity',
  'emitter.drag',
  'emitter.turbulence',
  'emitter.size',
  'emitter.stretch',
  'emitter.rotationSpeed',
  'mesh.radius',
  'mesh.width',
  'mesh.height',
  'ribbon.width',
  'ribbon.radius',
  'light.intensity',
  'post.strength',
  'post.radius',
  'life.size',
  'life.opacity',
  ...['position', 'rotation', 'scale'].flatMap((p) => [0, 1, 2].map((i) => `transform.${p}.${i}`)),
]);
export function validUniform(
  def: { type: string; min?: number; max?: number },
  v: unknown,
): boolean {
  if (def.type === 'float')
    return (
      typeof v === 'number' &&
      Number.isFinite(v) &&
      v >= (def.min ?? -100000) &&
      v <= (def.max ?? 100000)
    );
  if (def.type === 'color') return typeof v === 'string' && /^#[0-9a-f]{6}$/i.test(v);
  if (def.type === 'texture') return typeof v === 'string';
  return (
    Array.isArray(v) &&
    v.length === Number(def.type.slice(3)) &&
    v.every((n) => typeof n === 'number' && Number.isFinite(n) && Math.abs(n) <= 100000)
  );
}
export const curveSchema = z
  .object({
    interpolation: z.enum(['linear', 'smooth', 'step']).default('smooth'),
    domain: z.enum(['layer', 'life']).default('layer'),
    keys: z
      .array(z.object({ time: finite.min(0), value: finite.min(-100000).max(100000) }))
      .min(1)
      .max(64),
  })
  .strict()
  .superRefine((c, ctx) => {
    c.keys.forEach((k, i) => {
      if (i && k.time <= c.keys[i - 1].time)
        ctx.addIssue({
          code: 'custom',
          message: '曲线时间必须严格递增',
          path: ['keys', i, 'time'],
        });
      if (c.domain === 'life' && k.time > 1)
        ctx.addIssue({
          code: 'custom',
          message: '寿命曲线时间范围为 0–1',
          path: ['keys', i, 'time'],
        });
    });
  });
export const materialSchema = z
  .object({
    style: z.enum(['glow', 'smoke', 'fire', 'ring', 'spark', 'rune', 'electric', 'shield']),
    color,
    secondaryColor: color,
    intensity: finite.min(0).max(50),
    opacity: finite.min(0).max(1),
    blending: z.enum(['add', 'normal']),
    depthWrite: z.boolean(),
    softness: finite.min(0).max(5),
    textureId: z.string(),
    shaderId: z.string(),
    uniforms: z.record(z.string(), value),
  })
  .strict();
export const layerSchema = z
  .object({
    id: z.string().min(1).max(100),
    name: z.string().min(1).max(100),
    kind: z.enum(['particles', 'mesh', 'ribbon', 'light', 'postprocess']),
    enabled: z.boolean(),
    start: finite.min(0).max(120),
    duration: finite.min(0.02).max(120),
    transform: z.object({ position: vec3, rotation: vec3, scale: vec3 }).strict(),
    material: materialSchema,
    emitter: z
      .object({
        mode: z.enum(['burst', 'continuous']),
        count: z.number().int().min(1).max(5000),
        rate: finite.min(1).max(2000),
        emissionWindow: finite.min(0).max(120),
        lifetime: finite.min(0.02).max(30),
        lifetimeRandom: finite.min(0).max(0.95),
        shape: z.enum(['point', 'sphere', 'cone', 'ring', 'box']),
        radius: finite.min(0).max(30),
        angle: finite.min(0).max(180),
        speed: finite.min(0).max(50),
        speedRandom: finite.min(0).max(1),
        gravity: finite.min(-30).max(30),
        drag: finite.min(0).max(20),
        size: finite.min(0.001).max(20),
        sizeRandom: finite.min(0).max(0.95),
        stretch: finite.min(0.1).max(15),
        rotationSpeed: finite.min(-20).max(20),
        turbulence: finite.min(0).max(10),
      })
      .strict(),
    mesh: z
      .object({
        shape: z.enum(['sphere', 'plane', 'ring', 'torus', 'cone', 'cylinder', 'model']),
        radius: finite.min(0.001).max(50),
        height: finite.min(0.001).max(50),
        width: finite.min(0.001).max(50),
        thickness: finite.min(0.001).max(10),
        orientation: z.enum(['horizontal', 'vertical']),
        wireframe: z.boolean(),
        modelId: z.string(),
      })
      .strict(),
    ribbon: z
      .object({
        path: z.enum(['spiral', 'arc', 'bolt', 'orbit']),
        segments: z.number().int().min(8).max(256),
        width: finite.min(0.001).max(3),
        radius: finite.min(0.01).max(30),
        height: finite.min(-20).max(20),
        turns: finite.min(0.1).max(10),
        speed: finite.min(0).max(10),
        tail: finite.min(0.01).max(1),
        jitter: finite.min(0).max(5),
      })
      .strict(),
    light: z
      .object({
        intensity: finite.min(0).max(100),
        distance: finite.min(0.1).max(100),
        decay: finite.min(0).max(4),
      })
      .strict(),
    post: z
      .object({
        mode: z.enum(['distortion', 'flash', 'chromatic', 'custom']),
        strength: finite.min(0).max(3),
        radius: finite.min(0.01).max(30),
        shaderId: z.string(),
        stage: z.enum(['beforeBloom', 'afterBloom']),
      })
      .strict(),
    curves: z.record(z.string(), curveSchema),
  })
  .strict();
export const shaderSchema = z
  .object({
    id: z.string().min(1).max(100),
    name: z.string().min(1).max(100),
    profile: z.enum(['particle', 'mesh', 'post']),
    vertex: z.string().max(100000),
    fragment: z.string().max(100000),
    vertexPath: z.string().optional(),
    fragmentPath: z.string().optional(),
    uniforms: z
      .array(
        z
          .object({
            name: z.string().regex(/^u[A-Za-z0-9_]+$/),
            type: z.enum(['float', 'color', 'vec2', 'vec3', 'vec4', 'texture']),
            default: value,
            min: finite.optional(),
            max: finite.optional(),
            label: z.string().optional(),
          })
          .strict(),
      )
      .max(40),
  })
  .strict();
export const assetSchema = z
  .object({
    id: z.string().min(1).max(100),
    name: z.string().min(1).max(200),
    type: z.enum(['texture', 'model']),
    src: z.string().max(90000000),
    colorSpace: z.enum(['srgb', 'linear']),
    bytes: z.number().int().min(0),
  })
  .strict();
export const documentSchema = z
  .object({
    schemaVersion: z.literal(1),
    engineAbi: z.literal('ignis-webgl2-v1'),
    id: z.string().min(1),
    name: z.string().min(1).max(100),
    description: z.string().max(2000),
    duration: finite.min(0.1).max(120),
    loop: z.boolean(),
    seed: z.number().int().min(0).max(4294967295),
    layers: z.array(layerSchema).max(48),
    shaders: z.array(shaderSchema).max(48),
    assets: z.array(assetSchema).max(100),
    settings: z
      .object({
        bloom: z.boolean(),
        bloomStrength: finite.min(0).max(3),
        bloomThreshold: finite.min(0).max(10),
        bloomRadius: finite.min(0).max(1),
        exposure: finite.min(0.1).max(4),
        vignette: finite.min(0).max(1),
        chromatic: finite.min(0).max(0.05),
        resolutionScale: finite.min(0.5).max(2),
      })
      .strict(),
  })
  .strict()
  .superRefine((doc, ctx) => {
    for (const group of ['layers', 'shaders', 'assets'] as const) {
      const ids = new Set<string>();
      doc[group].forEach((item, i) => {
        if (ids.has(item.id))
          ctx.addIssue({ code: 'custom', path: [group, i, 'id'], message: 'ID 重复' });
        ids.add(item.id);
      });
    }
    if (doc.assets.reduce((n, a) => n + a.bytes, 0) > 128 * 1024 * 1024)
      ctx.addIssue({ code: 'custom', path: ['assets'], message: '项目资源预算最多 128 MiB' });
    for (const [sIndex, s] of doc.shaders.entries()) {
      const names = new Set<string>();
      for (const [uIndex, u] of s.uniforms.entries()) {
        if (names.has(u.name) || AUTO_UNIFORMS.has(u.name) || !validUniform(u, u.default))
          ctx.addIssue({
            code: 'custom',
            path: ['shaders', sIndex, 'uniforms', uIndex],
            message: 'Uniform 为自动输入、名称重复或默认值类型 / 范围不正确',
          });
        if (
          u.type === 'texture' &&
          u.default &&
          !doc.assets.some((a) => a.id === u.default && a.type === 'texture')
        )
          ctx.addIssue({
            code: 'custom',
            path: ['shaders', sIndex, 'uniforms', uIndex],
            message: 'Uniform 默认贴图引用不存在',
          });
        names.add(u.name);
      }
    }
    let particles = 0;
    doc.layers.forEach((l, i) => {
      if (l.start + l.duration > doc.duration + 0.001)
        ctx.addIssue({
          code: 'custom',
          path: ['layers', i, 'duration'],
          message: '图层结束时间超过效果时长',
        });
      if (l.kind === 'particles' && l.enabled) particles += l.emitter.count;
      for (const id of [l.material.shaderId, l.post.shaderId])
        if (id && !doc.shaders.some((s) => s.id === id))
          ctx.addIssue({
            code: 'custom',
            path: ['layers', i],
            message: `Shader 引用不存在：${id}`,
          });
      if (
        l.material.textureId &&
        !doc.assets.some((a) => a.id === l.material.textureId && a.type === 'texture')
      )
        ctx.addIssue({
          code: 'custom',
          path: ['layers', i, 'material'],
          message: '贴图引用不存在',
        });
      if (
        l.kind === 'mesh' &&
        l.mesh.shape === 'model' &&
        !doc.assets.some((a) => a.id === l.mesh.modelId && a.type === 'model')
      )
        ctx.addIssue({ code: 'custom', path: ['layers', i, 'mesh'], message: '模型引用不存在' });
      const shader = doc.shaders.find(
        (s) => s.id === (l.kind === 'postprocess' ? l.post.shaderId : l.material.shaderId),
      );
      for (const [name, v] of Object.entries(l.material.uniforms)) {
        const def = shader?.uniforms.find((u) => u.name === name);
        if (
          def?.type === 'texture' &&
          v &&
          !doc.assets.some((a) => a.id === v && a.type === 'texture')
        )
          ctx.addIssue({
            code: 'custom',
            path: ['layers', i, 'material', 'uniforms', name],
            message: 'Uniform 贴图引用不存在',
          });
        if (def && !validUniform(def, v))
          ctx.addIssue({
            code: 'custom',
            path: ['layers', i, 'material', 'uniforms', name],
            message: 'Uniform 类型或范围不正确',
          });
      }
      for (const [path, c] of Object.entries(l.curves)) {
        const uniform = path.startsWith('uniforms.')
          ? shader?.uniforms.find((u) => u.name === path.slice(9) && u.type === 'float')
          : undefined;
        if (!ANIMATABLE_PATHS.has(path) && !uniform)
          ctx.addIssue({
            code: 'custom',
            path: ['layers', i, 'curves', path],
            message: '该参数不支持动画',
          });
        for (const key of c.keys) {
          let valid = true;
          if (path === 'life.opacity' || path === 'material.opacity')
            valid = key.value >= 0 && key.value <= 1;
          if (path === 'life.size') valid = key.value >= 0 && key.value <= 10;
          if (
            path.startsWith('mesh.') ||
            path === 'ribbon.radius' ||
            path === 'ribbon.width' ||
            (path.startsWith('emitter.') &&
              path !== 'emitter.gravity' &&
              path !== 'emitter.rotationSpeed')
          )
            valid = key.value >= 0 && key.value <= 100;
          if (path === 'post.strength') valid = key.value >= 0 && key.value <= 3;
          if (uniform) valid = validUniform(uniform, key.value);
          if (!valid)
            ctx.addIssue({
              code: 'custom',
              path: ['layers', i, 'curves', path],
              message: '曲线数值超过参数有效范围',
            });
        }
      }
      const expected =
        l.kind === 'particles' ? 'particle' : l.kind === 'postprocess' ? 'post' : 'mesh';
      if (l.kind === 'light' && shader)
        ctx.addIssue({ code: 'custom', path: ['layers', i], message: '点光源不使用材质 Shader' });
      if (shader && shader.profile !== expected)
        ctx.addIssue({
          code: 'custom',
          path: ['layers', i],
          message: `Shader 需要 ${expected} 几何协议`,
        });
    });
    if (particles > 20000)
      ctx.addIssue({ code: 'custom', path: ['layers'], message: '启用粒子的槽位预算最多 20000' });
  });

export type EffectDocument = z.infer<typeof documentSchema>;
export type Layer = z.infer<typeof layerSchema>;
export type LayerKind = Layer['kind'];
export type Curve = z.infer<typeof curveSchema>;
export type ShaderAsset = z.infer<typeof shaderSchema>;
export type Asset = z.infer<typeof assetSchema>;
export type UniformValue = z.infer<typeof value>;
export type Diagnostic = {
  code: string;
  message: string;
  severity: 'error' | 'warning';
  targetId?: string;
  file?: string;
  line?: number;
  path?: string;
  revision?: number;
};
export type Vec3 = [number, number, number];
export const uid = (prefix = 'id') => `${prefix}_${randomUUID().slice(0, 8)}`;
export const KIND_LABEL: Record<LayerKind, string> = {
  particles: '粒子',
  mesh: '网格',
  ribbon: '拖尾',
  light: '灯光',
  postprocess: '后处理',
};
export function makeLayer(kind: LayerKind, patch: Partial<Layer> = {}): Layer {
  return layerSchema.parse({
    id: uid('layer'),
    name: `新建${KIND_LABEL[kind]}`,
    kind,
    enabled: true,
    start: 0,
    duration: 2.4,
    transform: { position: [0, 0.8, 0], rotation: [0, 0, 0], scale: [1, 1, 1] },
    material: {
      style: 'glow',
      color: '#69dfff',
      secondaryColor: '#2960cf',
      intensity: 3,
      opacity: 1,
      blending: 'add',
      depthWrite: false,
      softness: 0.3,
      textureId: '',
      shaderId: '',
      uniforms: {},
    },
    emitter: {
      mode: 'burst',
      count: 100,
      rate: 100,
      emissionWindow: 0.1,
      lifetime: 1.3,
      lifetimeRandom: 0.25,
      shape: 'sphere',
      radius: 0.05,
      angle: 35,
      speed: 3.5,
      speedRandom: 0.4,
      gravity: -2,
      drag: 0.4,
      size: 0.09,
      sizeRandom: 0.4,
      stretch: 1,
      rotationSpeed: 0,
      turbulence: 0.2,
    },
    mesh: {
      shape: 'sphere',
      radius: 0.5,
      height: 1,
      width: 1,
      thickness: 0.05,
      orientation: 'horizontal',
      wireframe: false,
      modelId: '',
    },
    ribbon: {
      path: 'spiral',
      segments: 80,
      width: 0.045,
      radius: 1.3,
      height: 1.5,
      turns: 1.3,
      speed: 1.3,
      tail: 0.65,
      jitter: 0.06,
    },
    light: { intensity: 6, distance: 9, decay: 2 },
    post: { mode: 'distortion', strength: 0.12, radius: 2, shaderId: '', stage: 'beforeBloom' },
    curves: {},
    ...patch,
  });
}
export function makeDocument(name = '未命名特效'): EffectDocument {
  return {
    schemaVersion: 1,
    engineAbi: 'ignis-webgl2-v1',
    id: uid('fx'),
    name,
    description: '',
    duration: 2.4,
    loop: true,
    seed: 240811,
    layers: [],
    shaders: [],
    assets: [],
    settings: {
      bloom: true,
      bloomStrength: 0.65,
      bloomThreshold: 1,
      bloomRadius: 0.45,
      exposure: 1,
      vignette: 0.22,
      chromatic: 0,
      resolutionScale: 1,
    },
  };
}
export function cloneDocument(doc: EffectDocument): EffectDocument {
  return {
    ...doc,
    layers: structuredClone(doc.layers),
    settings: { ...doc.settings },
    assets: doc.assets.map((a) => ({ ...a })),
    shaders: doc.shaders.map((s) => ({ ...s, uniforms: structuredClone(s.uniforms) })),
  };
}
export function parseDocument(input: unknown): EffectDocument {
  return documentSchema.parse(input);
}
export function getPath(obj: unknown, path: string): unknown {
  return path
    .split('.')
    .reduce<unknown>(
      (current, key) =>
        current !== null && typeof current === 'object'
          ? (current as Record<string, unknown>)[key]
          : undefined,
      obj,
    );
}
export function setPath(obj: unknown, path: string, val: unknown): void {
  const keys = path.split('.');
  if (keys.some((k) => !k || ['__proto__', 'prototype', 'constructor'].includes(k)))
    throw new Error('无效参数路径');
  let cursor = obj as Record<string, unknown>;
  for (const key of keys.slice(0, -1)) {
    if (!cursor[key] || typeof cursor[key] !== 'object') throw new Error(`参数路径不存在：${path}`);
    cursor = cursor[key] as Record<string, unknown>;
  }
  const last = keys.at(-1)!;
  if (!(last in cursor)) throw new Error(`参数不存在：${path}`);
  cursor[last] = val;
}
export function evaluateCurve(curve: Curve | undefined, t: number, fallback: number): number {
  if (!curve?.keys.length) return fallback;
  const keys = curve.keys;
  if (t <= keys[0].time) return keys[0].value;
  for (let i = 1; i < keys.length; i++) {
    if (t <= keys[i].time) {
      let f = (t - keys[i - 1].time) / (keys[i].time - keys[i - 1].time);
      if (curve.interpolation === 'step') f = 0;
      if (curve.interpolation === 'smooth') f = f * f * (3 - 2 * f);
      return keys[i - 1].value + (keys[i].value - keys[i - 1].value) * f;
    }
  }
  return keys.at(-1)!.value;
}
export function animated(layer: Layer, path: string, time: number, fallback?: number): number {
  const base = fallback ?? Number(getPath(layer, path));
  return evaluateCurve(layer.curves[path], time, Number.isFinite(base) ? base : 0);
}
export function hashString(str: string): number {
  let h = 2166136261;
  for (let i = 0; i < str.length; i++) {
    h ^= str.charCodeAt(i);
    h = Math.imul(h, 16777619);
  }
  return h >>> 0;
}
export function random(seed: number, index: number): number {
  let x = (seed ^ Math.imul(index + 1, 0x9e3779b9)) >>> 0;
  x ^= x >>> 16;
  x = Math.imul(x, 0x7feb352d);
  x ^= x >>> 15;
  x = Math.imul(x, 0x846ca68b);
  x ^= x >>> 16;
  return (x >>> 0) / 4294967296;
}
export function canonicalHash(doc: EffectDocument): string {
  return hashString(JSON.stringify(doc)).toString(16).padStart(8, '0');
}
