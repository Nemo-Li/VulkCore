import * as THREE from 'three';
import type { Diagnostic, ShaderAsset } from '../core/model';
import { RuntimeAssets } from './assets';
import { generateParticles, particleGeometry } from './particles';
import { makeLayer } from '../core/model';
import { shaderMaterial } from './nodes';

export async function compileShader(
  renderer: THREE.WebGLRenderer,
  shader: ShaderAsset,
  assets: RuntimeAssets,
  keepMaterial?: (material: THREE.ShaderMaterial) => void,
): Promise<Diagnostic[]> {
  const diagnostics: Diagnostic[] = [];
  for (const u of shader.uniforms) {
    const v = u.default;
    let valid = true;
    if (u.type === 'float')
      valid = typeof v === 'number' && v >= (u.min ?? -Infinity) && v <= (u.max ?? Infinity);
    if (u.type === 'color') valid = typeof v === 'string' && /^#[0-9a-f]{6}$/i.test(v);
    if (u.type.startsWith('vec')) valid = Array.isArray(v) && v.length === Number(u.type.slice(3));
    if (u.type === 'texture') valid = typeof v === 'string';
    if (!valid)
      diagnostics.push({
        code: 'UNIFORM_SCHEMA_ERROR',
        severity: 'error',
        targetId: shader.id,
        message: `${u.name} 默认值不符合 ${u.type} 定义`,
      });
  }
  if (diagnostics.length) return diagnostics;
  const material = shaderMaterial(shader, shader.profile, assets);
  material.uniforms.uTime.value = 0.4;
  material.uniforms.uProgress.value = 0.4;
  const layer = makeLayer('particles');
  layer.emitter.count = 1;
  layer.emitter.emissionWindow = 0;
  const geometry =
    shader.profile === 'particle'
      ? particleGeometry(generateParticles(layer, 1))
      : new THREE.PlaneGeometry(1, 1);
  const mesh = new THREE.Mesh(geometry, material);
  mesh.frustumCulled = false;
  const scene = new THREE.Scene();
  scene.add(mesh);
  const camera = new THREE.PerspectiveCamera(45, 1, 0.05, 20);
  camera.position.z = 3;
  camera.updateMatrixWorld();
  const previous = renderer.debug.onShaderError,
    previousCheck = renderer.debug.checkShaderErrors;
  renderer.debug.checkShaderErrors = true;
  renderer.debug.onShaderError = (gl, program, vs, fs) => {
    for (const [handle, stage, source] of [
      [vs, 'vert', material.vertexShader],
      [fs, 'frag', material.fragmentShader],
    ] as const) {
      const log = gl.getShaderInfoLog(handle) ?? '';
      if (!log.trim()) continue;
      const compiled = gl.getShaderSource(handle) ?? '';
      const offset = compiled.indexOf(source);
      const prefix = offset >= 0 ? compiled.slice(0, offset).split('\n').length - 1 : 0;
      for (const line of log.split('\n').filter((l) => /ERROR|error/.test(l))) {
        const match = line.match(/(?:ERROR:\s*\d+:|\b0\()\s*(\d+)/);
        diagnostics.push({
          code: 'SHADER_COMPILE_ERROR',
          severity: 'error',
          targetId: shader.id,
          file: `${shader.name}.${stage}.glsl`,
          line: match ? Math.max(1, Number(match[1]) - prefix) : undefined,
          message: line,
        });
      }
    }
    if (!diagnostics.length)
      diagnostics.push({
        code: 'SHADER_LINK_ERROR',
        severity: 'error',
        targetId: shader.id,
        message: gl.getProgramInfoLog(program) || 'Shader 程序链接失败',
      });
  };
  const target = new THREE.WebGLRenderTarget(4, 4),
    oldTarget = renderer.getRenderTarget();
  try {
    await renderer.compileAsync(scene, camera);
    renderer.setRenderTarget(target);
    renderer.render(scene, camera);
  } catch (e) {
    diagnostics.push({
      code: 'SHADER_COMPILE_ERROR',
      severity: 'error',
      targetId: shader.id,
      message: e instanceof Error ? e.message : String(e),
    });
  } finally {
    renderer.setRenderTarget(oldTarget);
    renderer.debug.onShaderError = previous;
    renderer.debug.checkShaderErrors = previousCheck;
    target.dispose();
    geometry.dispose();
    if (keepMaterial && !diagnostics.length) {
      for (const u of Object.values(material.uniforms))
        if (u.value instanceof THREE.Texture) u.value = null;
      keepMaterial(material);
    } else material.dispose();
  }
  return diagnostics;
}
