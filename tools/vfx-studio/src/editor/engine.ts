import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { TransformControls } from 'three/addons/controls/TransformControls.js';
import { EditorStore } from '../core/store';
import type { EffectDocument, Diagnostic } from '../core/model';
import { VfxRuntime, VfxInstance, VfxValidationError } from '../runtime/runtime';
import { VfxRenderer } from '../runtime/renderer';
import { createStage, type StageMode } from '../runtime/stage';
export type EngineMetrics = ReturnType<VfxRenderer['getMetrics']> & { fps: number };
export class EditorEngine {
  readonly scene = new THREE.Scene();
  readonly camera = new THREE.PerspectiveCamera(43, 1, 0.05, 150);
  readonly renderer: THREE.WebGLRenderer;
  readonly runtime: VfxRuntime;
  readonly pipeline: VfxRenderer;
  readonly orbit: OrbitControls;
  readonly transform: TransformControls;
  instance?: VfxInstance;
  lastGoodRevision = 0;
  shaderErrors: string[] = [];
  stage: ReturnType<typeof createStage>;
  private observer: ResizeObserver;
  private unsubscribe: () => void;
  private raf = 0;
  private disposed = false;
  private lastDoc: EffectDocument;
  private syncTask: Promise<void> = Promise.resolve();
  private generation = 0;
  private previousTime = 0;
  private metricTime = 0;
  private frameSamples: number[] = [];
  private dragging = false;
  private dragRevision = 0;
  private onMetrics: (m: EngineMetrics) => void;
  constructor(
    public canvas: HTMLCanvasElement,
    public store: EditorStore,
    onMetrics: (m: EngineMetrics) => void,
  ) {
    this.onMetrics = onMetrics;
    this.renderer = new THREE.WebGLRenderer({
      canvas,
      antialias: false,
      alpha: false,
      powerPreference: 'high-performance',
      preserveDrawingBuffer: true,
    });
    this.renderer.debug.onShaderError = (gl, p, vs, fs) => {
      const message = [gl.getProgramInfoLog(p), gl.getShaderInfoLog(vs), gl.getShaderInfoLog(fs)]
        .filter(Boolean)
        .join('\n');
      this.shaderErrors.push(message);
      store.diagnostics([{ code: 'RENDER_SHADER_ERROR', severity: 'error', message }]);
    };
    this.camera.position.set(6, 4.1, 7.6);
    this.orbit = new OrbitControls(this.camera, canvas);
    this.orbit.target.set(0, 1, 0);
    this.orbit.enableDamping = true;
    this.orbit.dampingFactor = 0.09;
    this.orbit.minDistance = 2;
    this.orbit.maxDistance = 28;
    this.orbit.maxPolarAngle = Math.PI * 0.49;
    this.orbit.update();
    this.stage = createStage(this.scene);
    this.runtime = new VfxRuntime(this.scene, this.camera, this.renderer);
    this.pipeline = new VfxRenderer(this.renderer, this.runtime);
    this.transform = new TransformControls(this.camera, canvas);
    this.transform.setSize(0.65);
    const helper = this.transform.getHelper();
    helper.userData.editorHelper = true;
    this.scene.add(helper);
    this.transform.enabled = false;
    helper.visible = false;
    this.transform.addEventListener('dragging-changed', (e) => {
      this.dragging = Boolean(e.value);
      this.orbit.enabled = !this.dragging;
      const object = this.transform.object;
      if (object) object.userData.transforming = this.dragging;
      if (this.dragging) {
        this.dragRevision = store.state.revision;
        store.playback.playing = false;
      } else if (object && store.state.selectedId) {
        const id = store.state.selectedId;
        try {
          store.apply(
            [
              {
                type: 'setParameter',
                targetId: id,
                name: 'transform.position',
                value: object.position.toArray(),
              },
              {
                type: 'setParameter',
                targetId: id,
                name: 'transform.rotation',
                value: [object.rotation.x, object.rotation.y, object.rotation.z],
              },
              {
                type: 'setParameter',
                targetId: id,
                name: 'transform.scale',
                value: object.scale.toArray(),
              },
            ],
            { expectedRevision: this.dragRevision, label: '调整空间变换' },
          );
        } catch (e) {
          store.status(e instanceof Error ? e.message : String(e));
        }
      }
    });
    this.lastDoc = store.state.doc;
    this.syncTask = this.sync();
    this.unsubscribe = store.subscribe(() => {
      if (store.state.doc !== this.lastDoc) {
        this.lastDoc = store.state.doc;
        this.syncTask = this.sync();
      }
      this.attachSelection();
    });
    this.observer = new ResizeObserver(() => this.resize());
    this.observer.observe(canvas.parentElement!);
    this.resize();
    this.raf = requestAnimationFrame(this.frame);
    let downX = 0,
      downY = 0;
    canvas.addEventListener('pointerdown', (e) => {
      downX = e.clientX;
      downY = e.clientY;
    });
    canvas.addEventListener('pointerup', (e) => {
      if (
        this.dragging ||
        Math.hypot(e.clientX - downX, e.clientY - downY) > 4 ||
        this.transform.axis
      )
        return;
      const rect = canvas.getBoundingClientRect(),
        p = new THREE.Vector2(
          ((e.clientX - rect.left) / rect.width) * 2 - 1,
          (-(e.clientY - rect.top) / rect.height) * 2 + 1,
        ),
        ray = new THREE.Raycaster();
      ray.setFromCamera(p, this.camera);
      const hit = ray
        .intersectObjects(this.instance?.root.children ?? [], true)
        .find((h) => h.object.parent?.userData.layerId);
      if (hit) store.select(hit.object.parent!.userData.layerId as string);
    });
  }
  async whenSynchronized() {
    await this.syncTask;
  }
  private async sync() {
    const generation = ++this.generation,
      revision = this.store.state.revision,
      doc = this.store.state.doc;
    try {
      await this.runtime.register(doc);
      if (this.disposed || generation !== this.generation) return;
      if (this.instance?.packageId !== doc.id) {
        const previous = this.instance?.packageId;
        this.instance?.dispose();
        if (previous) this.runtime.unload(previous);
        this.instance = this.runtime.play(doc.id, { loop: false });
      }
      this.lastGoodRevision = revision;
      this.store.diagnostics([], revision);
      this.attachSelection();
      this.resize();
    } catch (e) {
      if (this.disposed || generation !== this.generation) return;
      const d: Diagnostic[] =
        e instanceof VfxValidationError
          ? e.diagnostics
          : [
              {
                code: 'EFFECT_LOAD_ERROR',
                severity: 'error',
                message: e instanceof Error ? e.message : String(e),
              },
            ];
      this.store.diagnostics(
        d.map((x) => ({ ...x, revision })),
        revision,
      );
    }
  }
  private attachSelection() {
    const selected = this.store.state.selectedId,
      node = selected ? this.instance?.nodes.get(selected) : undefined;
    if (node && this.transform.object !== node.group) this.transform.attach(node.group);
    else if (!node) this.transform.detach();
    this.transform.getHelper().visible = this.transform.enabled && Boolean(node);
  }
  setTool(mode: 'translate' | 'rotate' | 'scale' | 'orbit') {
    if (mode === 'orbit') {
      this.transform.enabled = false;
      this.transform.getHelper().visible = false;
    } else {
      this.transform.enabled = true;
      this.transform.getHelper().visible = true;
      this.transform.setMode(mode);
      this.attachSelection();
    }
  }
  setStage(mode: StageMode) {
    const gridVisible = this.stage.grid.visible,
      referenceVisible = this.stage.reference.visible;
    this.stage.dispose();
    this.stage = createStage(this.scene, mode);
    this.stage.grid.visible = gridVisible;
    this.stage.reference.visible = referenceVisible;
  }
  resetCamera() {
    this.camera.position.set(6, 4.1, 7.6);
    this.orbit.target.set(0, 1, 0);
    this.orbit.update();
  }
  resize() {
    if (this.disposed) return;
    const rect = this.canvas.parentElement!.getBoundingClientRect(),
      scale = this.store.state.doc.settings.resolutionScale,
      pixelRatio = Math.min(devicePixelRatio, 1.6) * scale;
    this.renderer.setPixelRatio(pixelRatio);
    this.renderer.setSize(Math.max(1, rect.width), Math.max(1, rect.height), false);
    this.camera.aspect = rect.width / Math.max(1, rect.height);
    this.camera.updateProjectionMatrix();
    this.pipeline.setSize(rect.width * pixelRatio, rect.height * pixelRatio);
  }
  private frame = (now: number) => {
    if (this.disposed) return;
    this.raf = requestAnimationFrame(this.frame);
    const elapsed = this.previousTime ? now - this.previousTime : 16.67;
    this.previousTime = now;
    if (
      document.visibilityState === 'hidden' ||
      document.documentElement.dataset.hostHidden === 'true'
    )
      return;
    this.frameSamples.push(elapsed);
    if (this.frameSamples.length > 90) this.frameSamples.shift();
    const p = this.store.playback,
      d = this.store.state.doc;
    if (p.playing && !this.dragging) {
      p.time += Math.min(0.1, elapsed / 1000) * p.speed;
      if (p.time > d.duration) {
        if (p.loop) p.time %= d.duration;
        else {
          p.time = d.duration;
          p.playing = false;
        }
      }
    }
    this.orbit.update();
    if (this.instance) this.instance.seek(p.time);
    this.pipeline.render(this.store.soloId);
    if (now - this.metricTime > 300) {
      this.metricTime = now;
      this.onMetrics({
        ...this.pipeline.getMetrics(),
        fps: 1000 / (this.frameSamples.reduce((a, b) => a + b, 0) / this.frameSamples.length),
      });
    }
  };
  getMetrics() {
    return this.pipeline.getMetrics();
  }
  dispose() {
    this.disposed = true;
    this.generation++;
    cancelAnimationFrame(this.raf);
    this.observer.disconnect();
    this.unsubscribe();
    this.transform.dispose();
    this.orbit.dispose();
    this.stage.dispose();
    this.runtime.dispose();
    this.pipeline.dispose();
    this.renderer.dispose();
    this.renderer.forceContextLoss();
  }
}
