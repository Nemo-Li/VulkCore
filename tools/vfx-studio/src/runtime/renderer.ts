import * as THREE from 'three';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { TexturePass } from 'three/addons/postprocessing/TexturePass.js';
import { ShaderPass } from 'three/addons/postprocessing/ShaderPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';
import { fragmentComposite, vertexPost } from './shaders';
import { shaderMaterial, applyCustomUniforms } from './nodes';
import { VfxRuntime } from './runtime';
import { animated, hashString } from '../core/model';

type TimerExt = { TIME_ELAPSED_EXT: number; GPU_DISJOINT_EXT: number };
export class VfxRenderer {
  readonly depthTarget: THREE.WebGLRenderTarget;
  readonly sceneTarget: THREE.WebGLRenderTarget;
  readonly fieldTarget: THREE.WebGLRenderTarget;
  readonly composer: EffectComposer;
  private texturePass: TexturePass;
  private composite: ShaderPass;
  private bloom: UnrealBloomPass;
  private output: OutputPass;
  private custom = new Map<string, ShaderPass>();
  private customMasks = new Map<
    string,
    {
      target: THREE.WebGLRenderTarget;
      scene: THREE.Scene;
      proxy: THREE.Mesh<THREE.BufferGeometry, THREE.ShaderMaterial>;
    }
  >();
  private shaderHashes = new WeakMap<object, number>();
  private droppedCustomPasses = 0;
  private depthMaterial = new THREE.MeshDepthMaterial({ colorWrite: false });
  private width = 1;
  private height = 1;
  private timer: TimerExt | null;
  private query: WebGLQuery | null = null;
  private gpuMs: number | null = null;
  private gpuReason = 'pending';
  private cpuMs = 0;
  private passCount = 0;
  constructor(
    public renderer: THREE.WebGLRenderer,
    public runtime: VfxRuntime,
  ) {
    this.depthTarget = new THREE.WebGLRenderTarget(1, 1, {
      minFilter: THREE.NearestFilter,
      magFilter: THREE.NearestFilter,
    });
    this.depthTarget.depthTexture = new THREE.DepthTexture(1, 1, THREE.UnsignedIntType);
    this.sceneTarget = new THREE.WebGLRenderTarget(1, 1, { type: THREE.HalfFloatType });
    this.fieldTarget = new THREE.WebGLRenderTarget(1, 1, {
      type: THREE.HalfFloatType,
      depthBuffer: false,
    });
    const target = new THREE.WebGLRenderTarget(1, 1, {
      type: THREE.HalfFloatType,
      depthBuffer: false,
    });
    this.composer = new EffectComposer(renderer, target);
    this.composer.setPixelRatio(1);
    this.texturePass = new TexturePass(this.sceneTarget.texture);
    const material = new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3,
      vertexShader: vertexPost,
      fragmentShader: fragmentComposite,
      uniforms: {
        tDiffuse: { value: null },
        uField: { value: this.fieldTarget.texture },
        uChromatic: { value: 0 },
        uFlash: { value: 0 },
        uFlashColor: { value: new THREE.Color() },
        uVignette: { value: 0 },
      },
      depthTest: false,
      depthWrite: false,
      toneMapped: false,
    });
    this.composite = new ShaderPass(material);
    this.bloom = new UnrealBloomPass(new THREE.Vector2(1, 1), 0.65, 0.45, 1);
    this.output = new OutputPass();
    this.composer.addPass(this.texturePass);
    this.composer.addPass(this.composite);
    this.composer.addPass(this.bloom);
    this.composer.addPass(this.output);
    renderer.info.autoReset = false;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.timer = renderer
      .getContext()
      .getExtension('EXT_disjoint_timer_query_webgl2') as TimerExt | null;
    if (!this.timer) this.gpuReason = 'timerUnsupported';
  }
  setSize(width: number, height: number) {
    this.width = Math.max(1, Math.round(width));
    this.height = Math.max(1, Math.round(height));
    this.depthTarget.setSize(this.width, this.height);
    this.sceneTarget.setSize(this.width, this.height);
    this.fieldTarget.setSize(this.width, this.height);
    this.composer.setSize(this.width, this.height);
    for (const m of this.customMasks.values()) m.target.setSize(this.width, this.height);
  }
  render(soloId?: string | null) {
    const start = performance.now(),
      r = this.renderer,
      scene = this.runtime.scene,
      camera = this.runtime.camera,
      settings = this.runtime.settings();
    this.pollTimer();
    const gl = r.getContext() as WebGL2RenderingContext;
    if (this.timer && !this.query) {
      this.query = gl.createQuery();
      if (this.query) gl.beginQuery(this.timer.TIME_ELAPSED_EXT, this.query);
    }
    r.info.reset();
    r.toneMappingExposure = settings.exposure;
    const needDepth = [...this.runtime.instances].some(
      (i) =>
        !i.finished &&
        i.doc.layers.some(
          (l) =>
            l.enabled &&
            ((['particles', 'mesh', 'ribbon'].includes(l.kind) && l.material.softness > 0) ||
              (l.kind === 'postprocess' && ['distortion', 'custom'].includes(l.post.mode))),
        ),
    );
    this.runtime.prepare(
      {
        depth: this.depthTarget.depthTexture!,
        width: this.width,
        height: this.height,
        depthEnabled: needDepth,
      },
      soloId,
    );
    if (needDepth) {
      const hidden: THREE.Object3D[] = [];
      scene.traverse((o) => {
        const m = (o as THREE.Mesh).material;
        const transparent = m && (Array.isArray(m) ? m.some((v) => v.transparent) : m.transparent);
        if (o.visible && (o.userData.vfxRoot || o.userData.editorHelper || transparent)) {
          o.visible = false;
          hidden.push(o);
        }
      });
      const previous = scene.overrideMaterial;
      try {
        scene.overrideMaterial = this.depthMaterial;
        r.setRenderTarget(this.depthTarget);
        r.render(scene, camera);
      } finally {
        scene.overrideMaterial = previous;
        hidden.forEach((o) => (o.visible = true));
      }
    }
    r.setRenderTarget(this.sceneTarget);
    r.render(scene, camera);
    const clearColor = r.getClearColor(new THREE.Color()),
      alpha = r.getClearAlpha();
    r.setClearColor(0, 0);
    r.setRenderTarget(this.fieldTarget);
    r.clear();
    const distortion = this.runtime
      .contributions()
      .filter((c) => c.layer.post.mode === 'distortion');
    const fieldIds = new Set(distortion.map((c) => c.instance.id + ':' + c.layer.id));
    for (const i of this.runtime.instances)
      for (const [id, n] of i.nodes)
        if (n.field) n.fieldGroup.visible = fieldIds.has(i.id + ':' + id);
    if (distortion.length) r.render(this.runtime.fieldScene, camera);
    r.setClearColor(clearColor, alpha);
    const contributions = this.runtime.contributions();
    this.droppedCustomPasses = Math.max(
      0,
      contributions.filter((c) => c.layer.post.mode === 'custom').length - 8,
    );
    let customCount = 0;
    let chromatic = settings.chromatic,
      flash = 0;
    const flashColor = new THREE.Color(0);
    const before: ShaderPass[] = [],
      after: ShaderPass[] = [],
      used = new Set<string>();
    for (const c of contributions) {
      if (c.layer.post.mode === 'chromatic') chromatic = Math.max(chromatic, c.strength * 0.03);
      if (c.layer.post.mode === 'flash') {
        flash += c.strength;
        flashColor.add(new THREE.Color(c.layer.material.color).multiplyScalar(c.strength));
      }
      if (c.layer.post.mode === 'custom') {
        if (customCount++ >= 8) continue;
        const shader = c.instance.doc.shaders.find((s) => s.id === c.layer.post.shaderId);
        if (!shader) continue;
        let shaderHash = this.shaderHashes.get(shader);
        if (shaderHash === undefined) {
          shaderHash = hashString(JSON.stringify(shader));
          this.shaderHashes.set(shader, shaderHash);
        }
        const key = c.instance.id + ':' + c.layer.id + ':' + shaderHash;
        used.add(key);
        let pass = this.custom.get(key);
        if (!pass) {
          const mat = shaderMaterial(shader, 'post', c.instance.assets, c.layer);
          mat.depthTest = false;
          mat.depthWrite = false;
          mat.uniforms.uSceneColor = mat.uniforms.tDiffuse;
          pass = new ShaderPass(mat);
          pass.setSize(this.width, this.height);
          this.custom.set(key, pass);
        }
        applyCustomUniforms(pass.uniforms, shader, c.layer, c.instance.assets, c.time);
        const u = pass.uniforms;
        u.uTime.value = c.time;
        u.uEffectTime.value = c.instance.time;
        u.uProgress.value = c.time / c.layer.duration;
        u.uStrength.value = c.strength;
        u.uSeed.value = c.instance.seed;
        u.uIntensity.value = animated(c.layer, 'material.intensity', c.time) * c.instance.intensity;
        u.uSecondary.value.set(c.layer.material.secondaryColor);
        u.uTexture.value =
          c.instance.assets.textures.get(c.layer.material.textureId) ?? c.instance.assets.white;
        u.uHasTexture.value = c.instance.assets.textures.has(c.layer.material.textureId) ? 1 : 0;
        u.uCameraRight.value.setFromMatrixColumn(camera.matrixWorld, 0);
        u.uCameraUp.value.setFromMatrixColumn(camera.matrixWorld, 1);
        u.uOpacity.value = animated(c.layer, 'material.opacity', c.time);
        u.uTint.value.set(c.layer.material.color);
        u.uResolution.value.set(this.width, this.height);
        u.uNearFar.value.set(camera.near, camera.far);
        u.uSceneDepth.value = this.depthTarget.depthTexture;
        u.uEffectMask.value = this.fieldTarget.texture;
        const field = c.instance.nodes.get(c.layer.id)?.field;
        if (field) {
          let mask = this.customMasks.get(key);
          if (!mask) {
            const target = new THREE.WebGLRenderTarget(this.width, this.height, {
                type: THREE.HalfFloatType,
                depthBuffer: false,
              }),
              scene = new THREE.Scene(),
              proxy = new THREE.Mesh(field.geometry, field.material);
            proxy.frustumCulled = false;
            proxy.matrixAutoUpdate = false;
            scene.add(proxy);
            mask = { target, scene, proxy };
            this.customMasks.set(key, mask);
          }
          field.updateWorldMatrix(true, false);
          mask.proxy.geometry = field.geometry;
          mask.proxy.material = field.material;
          mask.proxy.matrix.copy(field.matrixWorld);
          r.setClearColor(0, 0);
          r.setRenderTarget(mask.target);
          r.clear();
          r.render(mask.scene, camera);
          r.setClearColor(clearColor, alpha);
          u.uEffectMask.value = mask.target.texture;
        }
        (c.layer.post.stage === 'beforeBloom' ? before : after).push(pass);
      }
    }
    for (const [key, pass] of this.custom)
      if (!used.has(key)) {
        pass.dispose();
        this.custom.delete(key);
        this.customMasks.get(key)?.target.dispose();
        this.customMasks.delete(key);
      }
    const u = this.composite.uniforms;
    u.uChromatic.value = chromatic;
    u.uFlash.value = flash;
    u.uFlashColor.value.copy(flashColor);
    u.uVignette.value = settings.vignette;
    this.bloom.enabled = settings.bloom;
    this.bloom.strength = settings.bloomStrength;
    this.bloom.threshold = settings.bloomThreshold;
    this.bloom.radius = settings.bloomRadius;
    this.composer.passes = [
      this.texturePass,
      this.composite,
      ...before,
      this.bloom,
      ...after,
      this.output,
    ];
    this.passCount = this.composer.passes.filter((p) => p.enabled).length;
    this.composer.render(0);
    if (this.query && this.timer && gl.getQuery(this.timer.TIME_ELAPSED_EXT, gl.CURRENT_QUERY))
      gl.endQuery(this.timer.TIME_ELAPSED_EXT);
    this.cpuMs = performance.now() - start;
  }
  private pollTimer() {
    if (!this.query || !this.timer) return;
    const gl = this.renderer.getContext() as WebGL2RenderingContext;
    if (gl.getParameter(this.timer.GPU_DISJOINT_EXT)) {
      gl.deleteQuery(this.query);
      this.query = null;
      this.gpuMs = null;
      this.gpuReason = 'disjoint';
      return;
    }
    if (gl.getQueryParameter(this.query, gl.QUERY_RESULT_AVAILABLE)) {
      this.gpuMs = Number(gl.getQueryParameter(this.query, gl.QUERY_RESULT)) / 1e6;
      gl.deleteQuery(this.query);
      this.query = null;
      this.gpuReason = '';
    }
  }
  getMetrics() {
    this.pollTimer();
    const info = this.renderer.info;
    return {
      ...this.runtime.getMetrics(),
      drawCalls: info.render.calls,
      triangles: info.render.triangles,
      cpuSubmitMs: this.cpuMs,
      gpuMs: this.gpuMs,
      gpuUnavailableReason: this.gpuMs === null ? this.gpuReason : null,
      renderWidth: this.width,
      renderHeight: this.height,
      passes: this.passCount,
      droppedCustomPasses: this.droppedCustomPasses,
      geometries: info.memory.geometries,
      textures: info.memory.textures,
      programs: info.programs?.length ?? 0,
    };
  }
  dispose() {
    const gl = this.renderer.getContext() as WebGL2RenderingContext;
    if (this.query) gl.deleteQuery(this.query);
    this.depthTarget.dispose();
    this.sceneTarget.dispose();
    this.fieldTarget.dispose();
    this.depthMaterial.dispose();
    for (const p of this.custom.values()) p.dispose();
    this.custom.clear();
    for (const m of this.customMasks.values()) m.target.dispose();
    this.customMasks.clear();
    for (const p of [this.texturePass, this.composite, this.bloom, this.output]) p.dispose();
    this.composer.dispose();
  }
}
