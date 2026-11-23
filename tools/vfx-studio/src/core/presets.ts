import {
  makeDocument,
  makeLayer,
  parseDocument,
  type Curve,
  type EffectDocument,
  type Layer,
  type LayerKind,
} from './model';

type Options = {
  id?: string;
  start?: number;
  duration?: number;
  position?: [number, number, number];
  rotation?: [number, number, number];
  material?: Partial<Layer['material']>;
  emitter?: Partial<Layer['emitter']>;
  mesh?: Partial<Layer['mesh']>;
  ribbon?: Partial<Layer['ribbon']>;
  post?: Partial<Layer['post']>;
  light?: Partial<Layer['light']>;
  curves?: Record<string, Curve>;
};
export const curve = (pairs: number[][], domain: 'layer' | 'life' = 'layer'): Curve => ({
  interpolation: 'smooth',
  domain,
  keys: pairs.map(([time, value]) => ({ time, value })),
});
function layer(kind: LayerKind, name: string, o: Options = {}): Layer {
  const l = makeLayer(kind);
  l.id = o.id ?? `layer_${name.replace(/\s/g, '_')}`;
  l.name = name;
  l.start = o.start ?? 0;
  l.duration = o.duration ?? 2.4;
  if (o.position) l.transform.position = o.position;
  if (o.rotation) l.transform.rotation = o.rotation;
  for (const key of ['material', 'emitter', 'mesh', 'ribbon', 'post', 'light'] as const)
    Object.assign(l[key], o[key]);
  l.curves = o.curves ?? {};
  return l;
}
const fade = (end = 1.2, peak = 1) =>
  curve([
    [0, 0],
    [0.06, peak],
    [end * 0.35, peak * 0.7],
    [end, 0],
  ]);
const life = {
  'life.size': curve(
    [
      [0, 0.1],
      [0.12, 1],
      [0.7, 0.8],
      [1, 0],
    ],
    'life',
  ),
  'life.opacity': curve(
    [
      [0, 0],
      [0.08, 1],
      [0.5, 0.9],
      [1, 0],
    ],
    'life',
  ),
};
export const PRESETS = [
  {
    id: 'arcane',
    name: '奥术迸发',
    en: 'ARCANE IMPACT',
    description: '符文、能量丝与扩散冲击波',
    color: '#71dcfa',
    tag: '命中',
    duration: 2.4,
  },
  {
    id: 'fire',
    name: '炽焰爆破',
    en: 'EMBER BURST',
    description: '火焰、余烬与翻滚烟尘',
    color: '#ff9c54',
    tag: '爆炸',
    duration: 3,
  },
  {
    id: 'portal',
    name: '星环传送门',
    en: 'ASTRAL GATE',
    description: '持续环流与旋转星尘',
    color: '#b397ff',
    tag: '循环',
    duration: 4,
  },
  {
    id: 'lightning',
    name: '雷霆落击',
    en: 'THUNDER STRIKE',
    description: '分叉电弧与地面电光',
    color: '#b4cdff',
    tag: '技能',
    duration: 1.8,
  },
  {
    id: 'shield',
    name: '棱光护盾',
    en: 'PRISM SHIELD',
    description: '菲涅耳球罩与扫描光环',
    color: '#65f2cf',
    tag: '防护',
    duration: 4,
  },
  {
    id: 'heal',
    name: '生命回响',
    en: 'VITAL ECHO',
    description: '上升萤光与柔和地面涟漪',
    color: '#adf69d',
    tag: '增益',
    duration: 3,
  },
  {
    id: 'frost',
    name: '霜晶绽放',
    en: 'FROST BLOOM',
    description: '低温雾气与冰晶碎屑',
    color: '#a0e9ff',
    tag: '冰霜',
    duration: 2.6,
  },
  {
    id: 'trail',
    name: '流星轨迹',
    en: 'COMET TRAIL',
    description: '运动弧线与散逸星光',
    color: '#f9c483',
    tag: '拖尾',
    duration: 3,
  },
] as const;
export function createPreset(id = 'arcane'): EffectDocument {
  const info = PRESETS.find((p) => p.id === id);
  if (!info) throw new Error('未知预设：' + id);
  const d = makeDocument(info.name);
  d.duration = info.duration;
  d.description = info.description;
  const color = info.color;
  const add = (kind: LayerKind, name: string, o: Options = {}) => {
    const l = layer(kind, name, { duration: d.duration, ...o, material: { color, ...o.material } });
    d.layers.push(l);
    return l;
  };
  if (id === 'arcane') {
    add('mesh', '01 · 符文基座', {
      id: 'layer_rune',
      position: [0, 0.04, 0],
      mesh: { shape: 'plane', width: 4.7, height: 4.7 },
      material: { style: 'rune', intensity: 2.6 },
      curves: {
        'material.opacity': fade(2.4),
        'transform.rotation.1': curve([
          [0, 0],
          [2.4, 0.5],
        ]),
      },
    });
    add('mesh', '02 · 能量核心', {
      id: 'layer_core',
      position: [0, 0.65, 0],
      mesh: { shape: 'sphere', radius: 0.35 },
      material: { style: 'shield', intensity: 5 },
      curves: {
        'mesh.radius': curve([
          [0, 0.03],
          [0.15, 0.38],
          [0.7, 0.24],
          [1.7, 0.01],
        ]),
        'material.opacity': fade(1.7),
      },
    });
    add('mesh', '03 · 冲击光环', {
      id: 'layer_ring',
      position: [0, 0.08, 0],
      mesh: { shape: 'plane', width: 2, height: 2 },
      material: { style: 'ring', intensity: 4 },
      curves: {
        'mesh.width': curve([
          [0, 0.3],
          [0.85, 7],
        ]),
        'mesh.height': curve([
          [0, 0.3],
          [0.85, 7],
        ]),
        'material.opacity': fade(0.9),
      },
    });
    add('particles', '04 · 辐射火花', {
      id: 'layer_sparks',
      emitter: { count: 150, speed: 4.7, lifetime: 1.5, size: 0.045, stretch: 4.5, gravity: -2.4 },
      material: { style: 'spark', intensity: 5.5 },
      curves: life,
    });
    add('particles', '05 · 漂浮微光', {
      id: 'layer_dust',
      start: 0.12,
      duration: 2.28,
      emitter: {
        count: 90,
        shape: 'sphere',
        radius: 0.6,
        speed: 0.65,
        lifetime: 2,
        size: 0.04,
        gravity: 0.2,
        turbulence: 0.35,
      },
      material: { intensity: 3.5 },
      curves: life,
    });
    add('ribbon', '06 · 环绕能量丝', {
      id: 'layer_ribbon',
      position: [0, 0.2, 0],
      ribbon: { path: 'spiral', radius: 1.6, height: 1.8, width: 0.035, turns: 1.3, tail: 0.8 },
      material: { style: 'electric', intensity: 5 },
      curves: { 'material.opacity': fade(2) },
    });
    add('light', '07 · 瞬时照明', {
      id: 'layer_light',
      position: [0, 0.7, 0],
      curves: { 'light.intensity': fade(1, 12) },
    });
    add('postprocess', '08 · 空间冲击', {
      id: 'layer_distortion',
      position: [0, 0.4, 0],
      post: { mode: 'distortion', radius: 2.8, strength: 0.1 },
      curves: {
        'post.strength': curve([
          [0, 0],
          [0.08, 0.18],
          [0.75, 0],
        ]),
      },
    });
  } else if (id === 'fire') {
    add('particles', '01 · 爆燃火团', {
      emitter: { count: 55, speed: 1.6, size: 0.65, lifetime: 1.2, gravity: 1.1, turbulence: 0.6 },
      material: { style: 'fire', intensity: 3.5, secondaryColor: '#b72712' },
      curves: life,
    });
    add('particles', '02 · 高温余烬', {
      emitter: { count: 220, speed: 5, size: 0.045, stretch: 5, lifetime: 2, gravity: -4 },
      material: { style: 'spark', intensity: 6 },
      curves: life,
    });
    add('particles', '03 · 翻滚烟尘', {
      start: 0.15,
      duration: 2.85,
      emitter: { count: 38, speed: 0.8, size: 1.2, lifetime: 2.6, gravity: 0.7, turbulence: 0.4 },
      material: {
        style: 'smoke',
        color: '#6b6260',
        intensity: 0.7,
        opacity: 0.35,
        blending: 'normal',
      },
      curves: {
        ...life,
        'life.size': curve(
          [
            [0, 0.3],
            [1, 1.6],
          ],
          'life',
        ),
      },
    });
    add('mesh', '04 · 地面冲击', {
      position: [0, 0.04, 0],
      mesh: { shape: 'plane', width: 2, height: 2 },
      material: { style: 'ring', intensity: 3 },
      curves: {
        'mesh.width': curve([
          [0, 0.1],
          [0.7, 8],
        ]),
        'mesh.height': curve([
          [0, 0.1],
          [0.7, 8],
        ]),
        'material.opacity': fade(0.75),
      },
    });
    add('light', '05 · 爆炸照明', {
      position: [0, 1, 0],
      curves: { 'light.intensity': fade(1, 20) },
    });
    add('postprocess', '06 · 热浪折射', {
      post: { strength: 0.12, radius: 3 },
      curves: { 'post.strength': fade(1.3, 0.14) },
    });
  } else if (id === 'portal') {
    add('mesh', '01 · 旋转星门', {
      position: [0, 1.8, 0],
      mesh: { shape: 'plane', width: 4, height: 4, orientation: 'vertical' },
      material: { style: 'rune', intensity: 3.4 },
      curves: {
        'transform.rotation.2': curve([
          [0, 0],
          [4, 3.14],
        ]),
        'material.opacity': fade(4),
      },
    });
    add('mesh', '02 · 星门内核', {
      position: [0, 1.8, -0.02],
      mesh: { shape: 'sphere', radius: 1.2 },
      material: { style: 'shield', opacity: 0.3, intensity: 2.5 },
      curves: { 'material.opacity': fade(4, 0.45) },
    });
    add('particles', '03 · 星尘流', {
      position: [0, 1.8, 0],
      rotation: [Math.PI / 2, 0, 0],
      emitter: {
        mode: 'continuous',
        count: 170,
        rate: 100,
        emissionWindow: 2.8,
        shape: 'ring',
        radius: 1.6,
        speed: 0.4,
        gravity: 0,
        size: 0.04,
        lifetime: 1.1,
      },
      material: { intensity: 6 },
      curves: life,
    });
    add('ribbon', '04 · 环绕光带', {
      position: [0, 1.8, 0],
      rotation: [Math.PI / 2, 0, 0],
      ribbon: { path: 'orbit', radius: 1.6, height: 0.2, width: 0.045, tail: 0.9, speed: 1.8 },
      material: { intensity: 5 },
      curves: { 'material.opacity': fade(4) },
    });
    add('light', '05 · 门内照明', {
      position: [0, 1.8, 0],
      curves: { 'light.intensity': fade(4, 8) },
    });
  } else if (id === 'lightning') {
    for (let i = 0; i < 3; i++)
      add('ribbon', `0${i + 1} · 雷电分支`, {
        position: [(i - 1) * 0.45, 0.1, 0],
        ribbon: {
          path: 'bolt',
          radius: 0.35 + i * 0.13,
          height: 4.4,
          width: i === 1 ? 0.08 : 0.035,
          jitter: 0.4,
          tail: 1,
        },
        material: { intensity: 8 },
        curves: {
          'material.opacity': curve([
            [0, 0],
            [0.05, 1],
            [0.18, 0.2],
            [0.22, 1],
            [0.48, 0.5],
            [0.7, 0],
          ]),
        },
      });
    add('particles', '04 · 触地点电火花', {
      position: [0, 0.1, 0],
      emitter: {
        count: 160,
        speed: 5,
        shape: 'cone',
        angle: 80,
        lifetime: 1,
        gravity: -7,
        size: 0.03,
        stretch: 5,
      },
      material: { style: 'spark', intensity: 7 },
      curves: life,
    });
    add('mesh', '05 · 电离地环', {
      position: [0, 0.03, 0],
      mesh: { shape: 'plane', width: 5, height: 5 },
      material: { style: 'rune', intensity: 5 },
      curves: { 'material.opacity': fade(1.1) },
    });
    add('light', '06 · 雷闪照明', { curves: { 'light.intensity': fade(0.8, 30) } });
  } else if (id === 'shield') {
    add('mesh', '01 · 菲涅耳护罩', {
      position: [0, 1.5, 0],
      mesh: { shape: 'sphere', radius: 1.6 },
      material: { style: 'shield', opacity: 0.8, intensity: 2.8 },
      curves: { 'material.opacity': fade(4, 0.8) },
    });
    add('mesh', '02 · 基底符文', {
      position: [0, 0.04, 0],
      mesh: { shape: 'plane', width: 4.4, height: 4.4 },
      material: { style: 'rune', intensity: 2.5 },
      curves: {
        'material.opacity': fade(4),
        'transform.rotation.1': curve([
          [0, 0],
          [4, 1],
        ]),
      },
    });
    add('ribbon', '03 · 扫描环', {
      position: [0, 0.7, 0],
      ribbon: { path: 'orbit', radius: 1.5, height: 0.8, tail: 1, width: 0.03 },
      curves: { 'material.opacity': fade(4) },
    });
    add('particles', '04 · 护盾微粒', {
      position: [0, 1.5, 0],
      emitter: {
        mode: 'continuous',
        count: 100,
        rate: 65,
        emissionWindow: 2.7,
        radius: 1.5,
        speed: 0.1,
        lifetime: 1.2,
        size: 0.035,
        gravity: 0.05,
      },
      curves: life,
    });
  } else if (id === 'heal') {
    add('particles', '01 · 生命萤火', {
      position: [0, 0.1, 0],
      emitter: {
        mode: 'continuous',
        count: 110,
        rate: 75,
        emissionWindow: 1.8,
        shape: 'cone',
        radius: 0.8,
        angle: 8,
        speed: 1.3,
        lifetime: 1.2,
        size: 0.06,
        gravity: 0.2,
        turbulence: 0.2,
      },
      material: { intensity: 3.5 },
      curves: life,
    });
    add('mesh', '02 · 治愈法阵', {
      position: [0, 0.04, 0],
      mesh: { shape: 'plane', width: 4, height: 4 },
      material: { style: 'rune', intensity: 2 },
      curves: { 'material.opacity': fade(3, 0.8) },
    });
    add('ribbon', '03 · 上升能量', {
      position: [0, 0, 0],
      ribbon: { radius: 0.9, height: 2.8, turns: 2.2, width: 0.035, tail: 0.7 },
      curves: { 'material.opacity': fade(3) },
    });
    add('light', '04 · 柔和照明', { curves: { 'light.intensity': fade(3, 6) } });
  } else if (id === 'frost') {
    add('particles', '01 · 冰晶碎屑', {
      emitter: {
        count: 170,
        speed: 3.2,
        shape: 'cone',
        angle: 65,
        lifetime: 1.7,
        size: 0.07,
        stretch: 2.5,
        gravity: -3,
      },
      material: { style: 'spark', intensity: 4 },
      curves: life,
    });
    add('particles', '02 · 低温雾', {
      position: [0, 0.2, 0],
      emitter: { count: 35, radius: 0.3, speed: 0.7, size: 1, lifetime: 2.2, gravity: 0.1 },
      material: { style: 'smoke', intensity: 0.9, opacity: 0.22, blending: 'normal' },
      curves: {
        ...life,
        'life.size': curve(
          [
            [0, 0.3],
            [1, 1.7],
          ],
          'life',
        ),
      },
    });
    add('mesh', '03 · 冰霜光环', {
      position: [0, 0.05, 0],
      mesh: { shape: 'plane', width: 5, height: 5 },
      material: { style: 'rune', intensity: 2.5 },
      curves: { 'material.opacity': fade(2.6) },
    });
    add('postprocess', '04 · 寒气折射', {
      post: { strength: 0.06, radius: 2.5 },
      curves: { 'post.strength': fade(1, 0.08) },
    });
  } else {
    for (let i = 0; i < 3; i++)
      add('ribbon', `0${i + 1} · 流星光带`, {
        position: [0, 0.3 + i * 0.12, 0],
        ribbon: {
          path: 'arc',
          radius: 2.5 + i * 0.1,
          height: 2.5,
          tail: 0.6,
          width: 0.06 - i * 0.015,
          speed: 1,
        },
        material: { intensity: 5 - i },
        curves: { 'material.opacity': fade(3) },
      });
    add('particles', '04 · 散逸星屑', {
      position: [0, 1, 0],
      emitter: { count: 150, radius: 1, speed: 0.5, lifetime: 2, size: 0.04, gravity: -0.5 },
      material: { intensity: 4 },
      curves: life,
    });
  }
  return parseDocument(d);
}
