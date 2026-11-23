import JSZip from 'jszip';
import * as THREE from 'three';
import { freezeDocument } from './package';
import type { EffectDocument } from '../core/model';
import { VfxRuntime } from '../runtime/runtime';
import { VfxRenderer } from '../runtime/renderer';

const cell = 192;
const gutter = 4;
const side = cell - 2 * gutter;
const toLinear = Array.from({ length: 256 }, (_, byte) => {
  const value = byte / 255;
  return value <= 0.04045 ? value / 12.92 : ((value + 0.055) / 1.055) ** 2.4;
});
const toSrgb = (value: number) =>
  255 * (value <= 0.0031308 ? 12.92 * value : 1.055 * value ** (1 / 2.4) - 0.055);

export async function exportFlipbook(
  input: EffectDocument,
  view: THREE.PerspectiveCamera,
  viewDistance: number,
  assertCurrent: () => void,
  progress: (frame: number, frames: number) => void,
) {
  const doc = await freezeDocument(input);
  // A vignette belongs to the preview camera. Applying it to a sprite leaves a visible box.
  doc.settings.vignette = 0;
  const frames = Math.min(256, Math.max(3, Math.ceil(doc.duration * 30)));
  const columns = Math.ceil(Math.sqrt(frames));
  const rows = Math.ceil(frames / columns);
  const atlas = document.createElement('canvas');
  atlas.width = columns * cell;
  atlas.height = rows * cell;
  const output = atlas.getContext('2d');
  const sample = document.createElement('canvas');
  sample.width = sample.height = side;
  const readback = sample.getContext('2d', { willReadFrequently: true });
  if (!output || !readback) throw new Error('无法创建精灵图画布');

  const camera = view.clone();
  camera.aspect = 1;
  camera.updateProjectionMatrix();
  camera.updateMatrixWorld();
  const clear = new THREE.Color(0x000000);
  const matte = new THREE.Color(0xeeeeee);
  const scene = new THREE.Scene();
  const renderer = new THREE.WebGLRenderer({ antialias: false, preserveDrawingBuffer: true });
  renderer.setSize(side, side, false);
  const runtime = new VfxRuntime(scene, camera, renderer);
  const pipeline = new VfxRenderer(renderer, runtime);
  pipeline.setSize(side, side);

  const capture = (background: THREE.Color) => {
    scene.background = background;
    pipeline.render();
    readback.drawImage(renderer.domElement, 0, 0);
    return readback.getImageData(0, 0, side, side).data;
  };
  try {
    await runtime.register(doc);
    const instance = runtime.play(doc.id, { loop: false });
    instance.seek(doc.duration + 0.01);
    const baseline = toLinear[capture(matte)[0]];
    const frame = output.createImageData(side, side);
    // Composite a black and a grey capture to estimate coverage. The emissive term is
    // kept in the sprite even where the source additive pass has no geometric coverage.
    for (let i = 0; i < frames; i++) {
      assertCurrent();
      instance.seek((doc.duration * i) / frames);
      const black = capture(clear);
      const grey = capture(matte);
      for (let px = 0; px < black.length; px += 4) {
        const red = toLinear[black[px]];
        const green = toLinear[black[px + 1]];
        const blue = toLinear[black[px + 2]];
        const transmission =
          (toLinear[grey[px]] +
            toLinear[grey[px + 1]] +
            toLinear[grey[px + 2]] -
            red -
            green -
            blue) /
          (3 * Math.max(baseline, 0.001));
        const alpha = Math.max(red, green, blue, Math.min(1, 1 - transmission));
        if (alpha <= 0.012) {
          frame.data.fill(0, px, px + 4);
          continue;
        }
        frame.data[px] = toSrgb(red / alpha);
        frame.data[px + 1] = toSrgb(green / alpha);
        frame.data[px + 2] = toSrgb(blue / alpha);
        frame.data[px + 3] = alpha * 255;
      }
      output.putImageData(
        frame,
        (i % columns) * cell + gutter,
        Math.floor(i / columns) * cell + gutter,
      );
      progress(i + 1, frames);
      await new Promise<number>((resolve) => requestAnimationFrame(resolve));
    }
  } finally {
    pipeline.dispose();
    runtime.dispose();
    renderer.dispose();
    renderer.forceContextLoss();
  }
  assertCurrent();
  const png = await new Promise<Blob>((resolve, reject) => {
    atlas.toBlob(
      (blob) => (blob ? resolve(blob) : reject(new Error('精灵图编码失败'))),
      'image/png',
    );
  });
  const worldWidth = THREE.MathUtils.clamp(
    (2 * viewDistance * Math.tan(THREE.MathUtils.degToRad(camera.fov * 0.5))) / camera.zoom,
    0.05,
    500,
  );
  const zip = new JSZip();
  zip.file('atlas.png', await png.arrayBuffer());
  zip.file(
    'effect.vfx',
    `vulkcore_flipbook 1\n${columns} ${rows} ${frames} ${cell} ${gutter} ${doc.duration} ${worldWidth}\natlas.png\n`,
  );
  return {
    blob: await zip.generateAsync({ type: 'blob' }),
    filename: `${doc.name}.vulkcore-vfx.zip`,
  };
}
