import assert from 'node:assert/strict';
import { test } from 'node:test';
import { nativeShader } from '../src/editor/native-shader';
import { nativePostFragment } from '../src/editor/native-post';
import { fragmentVfx, vertexMesh, vertexParticle, vertexPost } from '../src/runtime/shaders';
import glslang from '@webgpu/glslang';

test('Vulkan geometry and post profiles compile against the shared effect shader ABI', () => {
  // The Node distribution initializes the compiler synchronously.
  const compiler = glslang() as unknown as Awaited<ReturnType<typeof glslang>>;
  const variants = [
    nativeShader(vertexMesh, fragmentVfx, 'mesh'),
    nativeShader(vertexParticle, fragmentVfx, 'particle'),
    ...['distortion', 'chromatic', 'flash', 'custom'].map((mode) =>
      nativeShader(vertexPost, nativePostFragment(mode), 'post'),
    ),
  ];
  for (const [vertex, fragment] of variants) {
    assert.equal(compiler.compileGLSL(vertex, 'vertex', false)[0], 0x07230203);
    assert.equal(compiler.compileGLSL(fragment, 'fragment', false)[0], 0x07230203);
  }
});

test('Vulkan conversion diagnoses a mismatched varying before download', () => {
  assert.throws(
    () =>
      nativeShader(
        vertexPost,
        'in vec3 vUv; out vec4 outColor; void main(){outColor=vec4(vUv,1.0);}',
        'post',
      ),
    /varying vUv/,
  );
});
