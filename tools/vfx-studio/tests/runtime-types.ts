import * as THREE from 'three';
import type {
  VfxRuntime as PublicRuntime,
  VfxRenderer as PublicRenderer,
  EffectDocument as PublicDocument,
} from '../public/runtime/vfx-runtime.js';
import type { VfxRuntime as ImplementationRuntime } from '../src/runtime/runtime';
import type { VfxRenderer as ImplementationRenderer } from '../src/runtime/renderer';
import type { EffectDocument as ImplementationDocument } from '../src/core/model';
declare const implementation: ImplementationRuntime;
declare const rendererImplementation: ImplementationRenderer;
declare const document: ImplementationDocument;
const runtime: PublicRuntime = implementation;
const renderer: PublicRenderer = rendererImplementation;
const effect: PublicDocument = document;
const scene = new THREE.Scene();
void scene;
void effect;
const instance = runtime.play('fx_example', {
  position: [0, 0, 0],
  direction: [0, 0, 1],
  scale: 1,
  intensity: 1,
  seed: 42,
  loop: false,
});
instance.seek(0.5).stop();
renderer.render();
const gpu: number | null = renderer.getMetrics().gpuMs;
void gpu;
