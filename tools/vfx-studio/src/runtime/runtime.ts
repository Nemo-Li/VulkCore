import * as THREE from 'three';
import {
  animated,
  parseDocument,
  hashString,
  type Diagnostic,
  type EffectDocument,
  type Vec3,
} from '../core/model';
import { RuntimeAssets, loadEffectDocument } from './assets';
import { LayerNode } from './nodes';
import { compileShader } from './compiler';

export class VfxValidationError extends Error {
  constructor(public diagnostics: Diagnostic[]) {
    super(diagnostics.map((d) => d.message).join('\n'));
  }
}
export type PlayOptions = {
  position?: Vec3;
  direction?: Vec3;
  scale?: number;
  intensity?: number;
  seed?: number;
  loop?: boolean;
  parent?: THREE.Object3D;
};
type Package = { doc: EffectDocument; assets: RuntimeAssets; fingerprint: string };
type FrameContext = { depth: THREE.Texture; width: number; height: number; depthEnabled: boolean };

export class VfxInstance {
  readonly id = THREE.MathUtils.generateUUID();
  root = new THREE.Group();
  fieldRoot = new THREE.Group();
  nodes: Map<string, LayerNode>;
  time = 0;
  finished = false;
  loop = false;
  intensity = 1;
  stopTime: number | null = null;
  doc: EffectDocument;
  private seedOverride?: number;
  get seed() {
    return this.seedOverride ?? this.doc.seed;
  }
  constructor(
    public packageId: string,
    doc: EffectDocument,
    public assets: RuntimeAssets,
    private owner: VfxRuntime,
    options: PlayOptions = {},
    pooled?: Map<string, LayerNode>,
  ) {
    this.doc = doc;
    this.nodes = pooled ?? new Map();
    this.loop = options.loop ?? doc.loop;
    this.intensity = options.intensity ?? 1;
    this.seedOverride = options.seed;
    this.root.position.fromArray(options.position ?? [0, 0, 0]);
    this.root.scale.setScalar(options.scale ?? 1);
    if (options.direction) {
      const d = new THREE.Vector3().fromArray(options.direction).normalize();
      if (d.lengthSq()) this.root.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), d);
    }
    this.root.name = doc.name;
    this.root.userData.vfxRoot = true;
    (options.parent ?? owner.scene).add(this.root);
    owner.fieldScene.add(this.fieldRoot);
    this.reconcile(doc);
  }
  reconcile(doc: EffectDocument, assets: RuntimeAssets = this.assets) {
    if (assets !== this.assets) {
      for (const n of this.nodes.values()) n.dispose();
      this.nodes.clear();
      this.assets = assets;
    }
    this.doc = doc;
    const resolved = this.seedOverride === undefined ? doc : { ...doc, seed: this.seedOverride };
    const ids = new Set(doc.layers.map((l) => l.id));
    for (const [id, n] of this.nodes)
      if (!ids.has(id) || n.layer.kind !== doc.layers.find((l) => l.id === id)?.kind) {
        n.dispose();
        this.nodes.delete(id);
      }
    for (const l of doc.layers) {
      let n = this.nodes.get(l.id);
      if (!n) {
        n = new LayerNode(l, resolved, this.assets);
        this.nodes.set(l.id, n);
      } else n.patch(l, resolved);
      this.root.add(n.group);
      this.fieldRoot.add(n.fieldGroup);
    }
    this.nodes = new Map(doc.layers.map((layer) => [layer.id, this.nodes.get(layer.id)!]));
  }
  seek(time: number) {
    if (!Number.isFinite(time)) throw new Error('时间必须为有限数值');
    this.time = Math.max(0, time);
    this.finished = this.time > this.doc.duration;
    this.root.visible = !this.finished;
    return this;
  }
  stop(options: { immediate?: boolean } = {}) {
    this.loop = false;
    this.stopTime = this.time;
    if (options.immediate) {
      this.finished = true;
      this.root.visible = this.fieldRoot.visible = false;
    }
    return this;
  }
  update(dt: number) {
    if (this.finished) return;
    this.time += Math.max(0, dt);
    if (this.time > this.doc.duration) {
      if (this.loop && this.stopTime === null) this.time %= this.doc.duration;
      else {
        this.finished = true;
        this.root.visible = this.fieldRoot.visible = false;
      }
    }
  }
  prepare(camera: THREE.PerspectiveCamera, context: FrameContext, soloId?: string | null) {
    const visible = !this.finished && this.time <= this.doc.duration;
    this.root.visible = visible;
    this.fieldRoot.visible = visible;
    this.root.updateWorldMatrix(true, true);
    this.fieldRoot.matrixAutoUpdate = false;
    this.fieldRoot.matrix.copy(this.root.matrixWorld);
    this.fieldRoot.matrixWorld.copy(this.root.matrixWorld);
    let index = 0;
    for (const [id, n] of this.nodes) {
      n.update(
        this.time,
        camera,
        { ...context, intensity: this.intensity, stopTime: this.stopTime },
        index++,
      );
      if (soloId && id !== soloId) {
        n.group.visible = false;
        n.fieldGroup.visible = false;
      }
    }
  }
  contributions() {
    if (this.finished || this.stopTime !== null) return [];
    return this.doc.layers
      .filter(
        (l) =>
          l.enabled &&
          l.kind === 'postprocess' &&
          this.time >= l.start &&
          this.time <= l.start + l.duration,
      )
      .map((l) => ({
        instance: this,
        layer: l,
        time: this.time - l.start,
        strength: animated(l, 'post.strength', this.time - l.start) * this.intensity,
      }))
      .filter((c) => c.strength > 0.00001);
  }
  takeNodesForPool() {
    const nodes = this.nodes;
    this.nodes = new Map();
    for (const n of nodes.values()) {
      n.group.removeFromParent();
      n.fieldGroup.removeFromParent();
    }
    this.root.removeFromParent();
    this.fieldRoot.removeFromParent();
    this.owner.instances.delete(this);
    this.finished = true;
    return nodes;
  }
  dispose() {
    for (const n of this.nodes.values()) n.dispose();
    this.nodes.clear();
    this.root.removeFromParent();
    this.fieldRoot.removeFromParent();
    this.owner.instances.delete(this);
    this.finished = true;
  }
}

export class VfxRuntime {
  readonly fieldScene = new THREE.Scene();
  readonly instances = new Set<VfxInstance>();
  private packages = new Map<string, Package>();
  private compilation = new Map<string, Diagnostic[]>();
  private warmMaterials = new Map<string, THREE.ShaderMaterial>();
  private registration = new Map<string, number>();
  private validationQueue: Promise<void> = Promise.resolve();
  private pool = new Map<string, Map<string, LayerNode>[]>();
  private displayPackageId?: string;
  constructor(
    public scene: THREE.Scene,
    public camera: THREE.PerspectiveCamera,
    public renderer: THREE.WebGLRenderer,
  ) {}
  validate(doc: EffectDocument, assets?: RuntimeAssets): Promise<Diagnostic[]> {
    const task = this.validationQueue.then(() => this.validateNow(doc, assets));
    this.validationQueue = task.then(
      () => undefined,
      () => undefined,
    );
    return task;
  }
  private async validateNow(doc: EffectDocument, assets?: RuntimeAssets): Promise<Diagnostic[]> {
    const own = !assets,
      a = assets ?? new RuntimeAssets(),
      out: Diagnostic[] = [];
    try {
      if (own) await a.sync(doc);
      for (const s of doc.shaders) {
        const key = JSON.stringify(s);
        let diagnostics = this.compilation.get(key);
        if (!diagnostics) {
          diagnostics = await compileShader(this.renderer, s, a, (material) =>
            this.warmMaterials.set(key, material),
          );
          this.compilation.set(key, diagnostics);
          if (this.compilation.size > 128) {
            const first = this.compilation.keys().next().value!;
            this.compilation.delete(first);
            this.warmMaterials.get(first)?.dispose();
            this.warmMaterials.delete(first);
          }
        }
        out.push(...diagnostics);
      }
    } catch (e) {
      out.push({
        code: 'ASSET_LOAD_ERROR',
        severity: 'error',
        message: e instanceof Error ? e.message : String(e),
      });
    } finally {
      if (own) a.dispose();
    }
    return out;
  }
  async register(input: EffectDocument, baseUrl?: string): Promise<string> {
    const doc = parseDocument(input),
      token = (this.registration.get(doc.id) ?? 0) + 1;
    this.registration.set(doc.id, token);
    const previous = this.packages.get(doc.id),
      reuse =
        previous &&
        previous.doc.assets.length === doc.assets.length &&
        doc.assets.every((a, i) => {
          const p = previous.doc.assets[i];
          return (
            p.id === a.id && p.type === a.type && p.src === a.src && p.colorSpace === a.colorSpace
          );
        }),
      assets = reuse ? previous.assets : new RuntimeAssets();
    try {
      if (!reuse) await assets.sync(doc, baseUrl);
      const diagnostics = await this.validate(doc, assets);
      if (diagnostics.some((d) => d.severity === 'error'))
        throw new VfxValidationError(diagnostics);
      if (token !== this.registration.get(doc.id)) throw new Error('更新已被新版本替代');
      this.clearPool(doc.id);
      this.packages.set(doc.id, {
        doc,
        assets,
        fingerprint: String(hashString(JSON.stringify(doc))),
      });
      this.displayPackageId ??= doc.id;
      for (const i of this.instances) if (i.packageId === doc.id) i.reconcile(doc, assets);
      if (previous && previous.assets !== assets) previous.assets.dispose();
      return doc.id;
    } catch (e) {
      if (!reuse) assets.dispose();
      throw e;
    }
  }
  async preload(url: string) {
    return this.register(
      await loadEffectDocument(url),
      new URL(url, globalThis.location?.href).href,
    );
  }
  play(id: string, options: PlayOptions = {}) {
    const p = this.packages.get(id);
    if (!p) throw new Error(`请先 preload / register 特效：${id}`);
    if (this.instances.size >= 64) throw new Error('活跃实例预算最多 64 个');
    for (const v of [options.position, options.direction])
      if (v && (v.length !== 3 || v.some((n) => !Number.isFinite(n) || Math.abs(n) > 100000)))
        throw new Error('位置 / 方向不合法');
    for (const v of [options.scale, options.intensity])
      if (v !== undefined && (!Number.isFinite(v) || v < 0 || v > 100))
        throw new Error('尺寸 / 强度需要在 0–100 范围');
    const pooled = this.pool.get(id)?.pop(),
      i = new VfxInstance(id, p.doc, p.assets, this, options, pooled);
    this.instances.add(i);
    return i;
  }
  update(dt: number) {
    if (!Number.isFinite(dt)) throw new Error('dt 必须为有限数值');
    for (const i of this.instances) i.update(dt);
    this.collect();
  }
  prepare(context: FrameContext, soloId?: string | null) {
    this.camera.updateMatrixWorld();
    for (const i of this.instances) i.prepare(this.camera, context, soloId);
  }
  contributions() {
    return [...this.instances].flatMap((i) => i.contributions());
  }
  settings() {
    const base = this.packages.get(this.displayPackageId ?? '')?.doc.settings ?? {
      bloom: false,
      bloomStrength: 0,
      bloomThreshold: 1,
      bloomRadius: 0.4,
      exposure: 1,
      vignette: 0,
      chromatic: 0,
      resolutionScale: 1,
    };
    const active = [...this.instances].filter((i) => !i.finished).map((i) => i.doc.settings),
      blooming = active.filter((s) => s.bloom);
    return {
      ...base,
      bloom: blooming.length > 0,
      bloomStrength: Math.max(0, ...blooming.map((s) => s.bloomStrength)),
      bloomThreshold: blooming.length
        ? Math.min(...blooming.map((s) => s.bloomThreshold))
        : base.bloomThreshold,
      bloomRadius: Math.max(0, ...blooming.map((s) => s.bloomRadius)),
    };
  }
  getMetrics() {
    let particles = 0,
      slots = 0,
      layers = 0;
    for (const i of this.instances)
      if (!i.finished)
        for (const n of i.nodes.values())
          if (n.group.visible) {
            layers++;
            if (n.particleData) {
              slots += n.particleData.starts.length;
              particles += n.aliveCount(i.time, i.stopTime);
            }
          }
    return {
      activeInstances: [...this.instances].filter((i) => !i.finished).length,
      pooledInstances: [...this.pool.values()].reduce((n, list) => n + list.length, 0),
      visibleLayers: layers,
      particleSlots: slots,
      activeParticles: particles,
    };
  }
  stopAll() {
    for (const i of this.instances) i.stop({ immediate: true });
  }
  collect() {
    let count = [...this.pool.values()].reduce((n, list) => n + list.length, 0);
    for (const i of [...this.instances])
      if (i.finished) {
        const nodes = i.takeNodesForPool();
        if (count < 16) {
          const list = this.pool.get(i.packageId) ?? [];
          list.push(nodes);
          this.pool.set(i.packageId, list);
          count++;
        } else for (const n of nodes.values()) n.dispose();
      }
  }
  private clearPool(id?: string) {
    for (const [key, list] of this.pool)
      if (id === undefined || key === id) {
        for (const nodes of list) for (const n of nodes.values()) n.dispose();
        this.pool.delete(key);
      }
  }
  unload(id: string) {
    if ([...this.instances].some((i) => i.packageId === id))
      throw new Error('先释放引用该资源的实例');
    this.clearPool(id);
    this.packages.get(id)?.assets.dispose();
    this.packages.delete(id);
    this.registration.delete(id);
    if (this.displayPackageId === id) this.displayPackageId = this.packages.keys().next().value;
  }
  dispose() {
    for (const i of [...this.instances]) i.dispose();
    this.clearPool();
    for (const p of this.packages.values()) p.assets.dispose();
    this.packages.clear();
    this.compilation.clear();
    for (const m of this.warmMaterials.values()) m.dispose();
    this.warmMaterials.clear();
  }
}
