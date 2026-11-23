import { cleanGLSL } from '../runtime/shaders';
import type { ShaderAsset } from '../core/model';

export const PARAMETER_COUNT = 96;
export const CUSTOM_START = 40;
export const CUSTOM_TEXTURES = 4;

const uniforms: Record<string, [string, string]> = {
  uTime: ['float', 'fx.values[0].x'],
  uEffectTime: ['float', 'fx.values[0].y'],
  uProgress: ['float', 'fx.values[0].z'],
  uSeed: ['float', 'fx.values[0].w'],
  uIntensity: ['float', 'fx.values[1].x'],
  uOpacity: ['float', 'fx.values[1].y'],
  uStyle: ['float', 'fx.values[1].z'],
  uIsRibbon: ['float', 'fx.values[1].w'],
  uSoftness: ['float', 'fx.values[2].x * scene.model[0][0]'],
  uHasTexture: ['float', 'fx.values[2].y'],
  uDepthEnabled: ['float', 'fx.values[2].z'],
  uWorldScale: ['float', 'fx.values[2].w'],
  uTint: ['vec3', 'fx.values[3].xyz'],
  uSecondary: ['vec3', 'fx.values[4].xyz'],
  uSpeed: ['float', 'fx.values[5].x'],
  uGravity: ['float', 'fx.values[5].y'],
  uDrag: ['float', 'fx.values[5].z'],
  uTurbulence: ['float', 'fx.values[5].w'],
  uSize: ['float', 'fx.values[6].x'],
  uStretch: ['float', 'fx.values[6].y'],
  uRotationSpeed: ['float', 'fx.values[6].z'],
  uEmitterMode: ['float', 'fx.values[6].w'],
  uCycle: ['float', 'fx.values[7].x'],
  uEmissionWindow: ['float', 'fx.values[7].y'],
  uStrength: ['float', 'fx.values[7].z'],
  uResolution: ['vec2', 'scene.viewport.xy'],
  uNearFar: ['vec2', 'fx.values[8].xy'],
  uPostCenter: ['vec3', 'fx.values[9].xyz'],
  uPostRadius: ['float', 'fx.values[9].w'],
  uCameraRight: ['vec3', 'transpose(mat3(scene.view))[0]'],
  uCameraUp: ['vec3', 'transpose(mat3(scene.view))[1]'],
  modelMatrix: ['mat4', 'fx.model'],
  viewMatrix: ['mat4', 'scene.view'],
  modelViewMatrix: ['mat4', 'scene.view * fx.model'],
  projectionMatrix: ['mat4', 'fxGlProjection()'],
  normalMatrix: ['mat3', 'transpose(inverse(mat3(scene.view * fx.model)))'],
  cameraPosition: ['vec3', 'scene.camera.xyz'],
  isOrthographic: ['bool', 'scene.projection[3][3] != 0.0'],
};

const attributes: Record<string, [number, string]> = {
  position: [0, 'vec3'],
  normal: [1, 'vec3'],
  uv: [2, 'vec2'],
  aPosition: [3, 'vec3'],
  aVelocity: [4, 'vec3'],
  aStart: [5, 'float'],
  aLifetime: [6, 'float'],
  aSize: [7, 'float'],
  aRotation: [8, 'float'],
  aRandom: [9, 'float'],
};

export function customSlots(shader?: ShaderAsset) {
  let value = CUSTOM_START;
  let texture = 0;
  return (shader?.uniforms ?? []).map((u) => {
    if (u.type === 'texture') {
      if (texture >= CUSTOM_TEXTURES) throw new Error('Vulkan 每层支持最多 4 张自定义贴图');
      return { ...u, slot: texture++ };
    }
    if (value >= PARAMETER_COUNT) throw new Error('Vulkan 自定义 Uniform 数量超出限制');
    const slot = value;
    value += 4;
    return { ...u, slot };
  });
}

const sceneBlock = `
layout(std140, set=0,binding=0) uniform Scene {
  mat4 model; mat4 view; mat4 projection;
  vec4 time; vec4 viewport; vec4 ambient; vec4 sunDirection; vec4 sunColor; vec4 camera;
} scene;
layout(std140, set=0,binding=1) uniform Layer {
  mat4 model; vec4 values[${PARAMETER_COUNT / 4}]; vec4 life[128];
} fx;
// Match Three.js clip space for authored shaders, then convert gl_Position at the entry point.
mat4 fxGlProjection() {
  mat4 m = scene.projection;
  for (int i=0; i<4; ++i) {
    m[i].y = -m[i].y;
    m[i].z = 2.0*m[i].z-m[i].w;
  }
  return m;
}
vec4 fxLife(vec2 uv) {
  float p = clamp(uv.x*128.0-0.5,0.0,127.0);
  int i = int(p);
  return mix(fx.life[i],fx.life[min(i+1,127)],fract(p));
}
`;

export function nativeShader(
  vertex: string,
  fragment: string,
  profile: 'particle' | 'mesh' | 'post',
  shader?: ShaderAsset,
) {
  const varying = new Map<string, { location: number; type: string; interpolation: string }>();
  const values = { ...uniforms };
  const samplers = new Map([
    ['uTexture', 2],
    ['uSceneDepth', 3],
    ['tDiffuse', 8],
    ['uSceneColor', 8],
    ['uEffectMask', 9],
  ]);
  for (const u of customSlots(shader)) {
    if (u.type === 'texture') samplers.set(u.name, 4 + u.slot);
    else {
      const suffix = {
        float: '.x',
        vec2: '.xy',
        vec3: '.xyz',
        vec4: '',
        color: '.xyz',
      }[u.type];
      const expr = `fx.values[${u.slot / 4}]${suffix}`;
      values[u.name] = [u.type === 'color' ? 'vec3' : u.type, expr];
    }
  }

  const convert = (input: string, stage: 'vertex' | 'fragment') => {
    const declarations: string[] = [];
    const macros = new Set<string>();
    const declareSampler = (name: string) => {
      const binding = samplers.get(name);
      if (binding === undefined) throw new Error(`不支持的 sampler2D ${name}`);
      const native = `fxSampler${binding}`;
      const declaration = `layout(set=0,binding=${binding}) uniform sampler2D ${native};`;
      if (!declarations.includes(declaration)) declarations.push(declaration);
      macros.add(`#define ${name} ${native}`);
    };
    let source = cleanGLSL(input)
      .replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*/g, (comment) => comment.replace(/[^\n]/g, ' '))
      .replace(/^\s*#extension\s+[^\n]*/gm, '')
      .replace(/\bprecision\s+\w+\s+\w+\s*;/g, '')
      .replace(
        /\buniform\s+(?:(?:highp|mediump|lowp)\s+)?(\w+)\s+([\w\s,]+);/g,
        (_all, type: string, list: string) => {
          for (const name of list.split(',').map((n) => n.trim())) {
            if (
              type === 'sampler2D' &&
              (name === 'uLifeCurve' || (profile === 'post' && name === 'uEffectMask'))
            )
              continue;
            if (type === 'sampler2D') declareSampler(name);
            else {
              if (values[name]?.[0] !== type)
                throw new Error(`Uniform ${name} 缺少对应的 Vulkan 类型或元数据`);
              macros.add(`#define ${name} (${values[name][1]})`);
            }
          }
          return '';
        },
      )
      .replace(
        /(?:layout\s*\(\s*location\s*=\s*\d+\s*\)\s*)?\b(?:(flat|smooth|noperspective)\s+)?(in|out)\s+(?:(?:highp|mediump|lowp)\s+)?(\w+)\s+([\w\s,]+);/g,
        (_all, interpolation: string, direction: string, type: string, list: string) =>
          list
            .split(',')
            .map((n) => {
              const name = n.trim();
              const qualifier = interpolation ?? 'smooth';
              let location = 0;
              if (stage === 'vertex' && direction === 'in') {
                if (
                  attributes[name]?.[1] !== type ||
                  (profile !== 'particle' && attributes[name][0] > 2)
                )
                  throw new Error(`不支持的顶点属性 ${name}`);
                location = attributes[name][0];
              } else if (stage === 'vertex') {
                location = varying.size;
                varying.set(name, { type, location, interpolation: qualifier });
              } else if (direction === 'in') {
                const output = varying.get(name);
                if (!output || output.type !== type || output.interpolation !== qualifier)
                  throw new Error(`顶点与片元着色器的 varying ${name} 不匹配`);
                location = output.location;
              } else if (type !== 'vec4' || list.includes(',')) {
                throw new Error('Vulkan 特效片元着色器需要一个 vec4 颜色输出');
              }
              return `layout(location=${location}) ${interpolation ?? ''} ${direction} ${type} ${name};`;
            })
            .join('\n'),
      )
      .replace(/\btexture\s*\(\s*uLifeCurve\s*,/g, 'fxLife(')
      .replace(/\bgl_VertexID\b/g, 'gl_VertexIndex')
      .replace(/\bgl_InstanceID\b/g, 'gl_InstanceIndex');
    if (profile === 'post' && /\btexture\s*\(\s*uEffectMask\s*,/.test(source)) {
      declareSampler('uSceneDepth');
      source = source.replace(/\btexture\s*\(\s*uEffectMask\s*,/g, 'fxPostMask(fxSampler3,');
    }
    if (stage === 'vertex') {
      for (const [name, [location, type]] of Object.entries(attributes)) {
        if (location > 2 && profile !== 'particle') continue;
        if (
          new RegExp(`\\b${name}\\b`).test(source) &&
          !new RegExp(`\\bin\\s+${type}\\s+${name}\\s*;`).test(source)
        )
          declarations.push(`layout(location=${location}) in ${type} ${name};`);
      }
      source = source.replace(/\bvoid\s+main\s*\(/, 'void fxMain(');
      source +=
        '\nvoid main(){fxMain();gl_Position.y=-gl_Position.y;gl_Position.z=(gl_Position.z+gl_Position.w)*0.5;}';
    } else {
      // WebGL fragment coordinates are bottom-left. The depth and color targets in Vulkan are top-left.
      source = source.replace(
        /\bgl_FragCoord\b/g,
        'vec4(gl_FragCoord.x, scene.viewport.y-gl_FragCoord.y, gl_FragCoord.zw)',
      );
      source = source.replace(
        /\btexture\s*\(\s*(uSceneDepth|uSceneColor|tDiffuse|uEffectMask)\s*,/g,
        'fxScreenTexture($1,',
      );
    }
    for (const name of [
      'modelMatrix',
      'modelViewMatrix',
      'viewMatrix',
      'projectionMatrix',
      'normalMatrix',
      'cameraPosition',
      'isOrthographic',
    ]) {
      macros.add(`#define ${name} (${values[name][1]})`);
    }
    return `#version 450\n${sceneBlock}
vec4 fxScreenTexture(sampler2D tex, vec2 uv){return texture(tex,vec2(uv.x,1.0-uv.y));}
vec4 fxPostMask(sampler2D depthTexture, vec2 uv) {
  vec2 ndc = uv*2.0-1.0;
  mat4 projectionInverse = inverse(fxGlProjection());
  vec4 a = projectionInverse*vec4(ndc,-1.0,1.0), b = projectionInverse*vec4(ndc,1.0,1.0);
  vec3 rayStart = a.xyz/a.w, rayEnd = b.xyz/b.w;
  vec4 centerAndRadius = fx.values[9];
  vec3 hit = mix(rayStart, rayEnd, (centerAndRadius.z-rayStart.z)/(rayEnd.z-rayStart.z));
  if (hit.z >= 0.0) return vec4(0.0);
  float r = length((hit.xy-centerAndRadius.xy)/max(0.0001,centerAndRadius.w));
  float mask = exp(-pow((r-mix(.04,.95,fx.values[0].z))*18.0,2.0))*(1.0-smoothstep(.9,1.0,r));
  float depth = fxScreenTexture(depthTexture, uv).r;
  vec4 sceneDepth = projectionInverse*vec4(ndc,depth*2.0-1.0,1.0);
  mask *= clamp((-sceneDepth.z/sceneDepth.w+hit.z)*4.0,0.0,1.0);
  return vec4(0.0,0.0,0.0,mask);
}
${declarations.join('\n')}\n${[...macros].join('\n')}\n${source}`;
  };
  return [convert(vertex, 'vertex'), convert(fragment, 'fragment')] as const;
}
