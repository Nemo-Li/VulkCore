import * as THREE from 'three';
import { cloneDocument, uid, type EffectDocument, type Diagnostic } from '../core/model';
import { VfxRuntime, VfxValidationError } from '../runtime/runtime';
import { VfxRenderer } from '../runtime/renderer';
import { createStage } from '../runtime/stage';
import { bytesToBase64, freezeDocument, sha256 } from './package';
export type PreviewOptions = {
  seed?: number;
  scene?: 'dark_ground' | 'light_ground' | 'arena';
  camera?: 'three_quarter' | 'front' | 'top';
  times?: number[];
  width?: number;
  height?: number;
  outputs?: ('contactSheet' | 'frames' | 'metrics' | 'video')[];
};
export type MediaArtifact = {
  artifactId: string;
  kind: string;
  mimeType: string;
  filename: string;
  dataBase64: string;
  times?: number[];
};
export type PreviewJob = {
  jobId: string;
  status: 'queued' | 'running' | 'succeeded' | 'failed' | 'cancelled';
  effectId: string;
  revision: number;
  snapshotId?: string;
  docHash?: string;
  sourceDocHash?: string;
  progress: string;
  artifacts: MediaArtifact[];
  metrics?: ReturnType<VfxRenderer['getMetrics']>;
  diagnostics: Diagnostic[];
  cancelled: boolean;
  input: PreviewOptions;
  createdAt: string;
  rendererInfo?: Record<string, unknown>;
  sampleMetrics?: { time: number; metrics: ReturnType<VfxRenderer['getMetrics']> }[];
  metricsAggregation?: string;
};
const nextFrame = () => new Promise<void>((resolve) => requestAnimationFrame(() => resolve()));
export class PreviewQueue {
  jobs = new Map<string, PreviewJob>();
  private queue: Promise<void> = Promise.resolve();
  private completions = new Map<string, Promise<PreviewJob>>();
  start(doc: EffectDocument, revision: number, options: PreviewOptions = {}) {
    if (
      [...this.jobs.values()].filter((j) => j.status === 'queued' || j.status === 'running')
        .length >= 4
    )
      throw new Error('预览队列最多 4 个待完成任务');
    const job: PreviewJob = {
      jobId: uid('job'),
      status: 'queued',
      effectId: doc.id,
      revision,
      progress: '排队中',
      artifacts: [],
      diagnostics: [],
      cancelled: false,
      input: structuredClone(options),
      createdAt: new Date().toISOString(),
    };
    this.jobs.set(job.jobId, job);
    const snapshot = cloneDocument(doc);
    const task = this.queue.then(async () => {
      if (job.cancelled) {
        job.status = 'cancelled';
        return;
      }
      await this.run(job, snapshot);
    });
    this.queue = task.catch(() => undefined);
    const complete = task.then(() => job);
    this.completions.set(job.jobId, complete);
    if (this.jobs.size > 20) {
      for (const [id, j] of this.jobs) {
        if (j.status === 'succeeded' || j.status === 'failed' || j.status === 'cancelled') {
          this.jobs.delete(id);
          this.completions.delete(id);
          break;
        }
      }
    }
    return job;
  }
  get(id: string) {
    const j = this.jobs.get(id);
    if (!j) throw new Error('预览任务不存在或已过期');
    return j;
  }
  async wait(id: string) {
    const p = this.completions.get(id);
    if (!p) throw new Error('任务不存在');
    return p;
  }
  cancel(id: string) {
    const j = this.get(id);
    if (['succeeded', 'failed', 'cancelled'].includes(j.status))
      return { jobId: id, status: j.status, cancelRequested: false };
    j.cancelled = true;
    if (j.status === 'queued') j.status = 'cancelled';
    return { jobId: id, status: j.status, cancelRequested: true };
  }
  private async run(job: PreviewJob, doc: EffectDocument) {
    let renderer: THREE.WebGLRenderer | undefined,
      runtime: VfxRuntime | undefined,
      pipeline: VfxRenderer | undefined,
      stage: ReturnType<typeof createStage> | undefined;
    const check = () => {
      if (job.cancelled) throw new Error('JOB_CANCELLED');
    };
    try {
      job.status = 'running';
      job.progress = '冻结资源';
      doc = await freezeDocument(doc);
      job.docHash = await sha256(doc);
      job.sourceDocHash = job.docHash;
      check();
      const o = job.input,
        width = o.width ?? 800,
        height = o.height ?? 500,
        times = o.times ?? [
          0,
          0.12 * doc.duration,
          0.3 * doc.duration,
          0.6 * doc.duration,
          0.9 * doc.duration,
          doc.duration + 0.1,
        ];
      if (times.some((t) => t > doc.duration + 2)) throw new Error('采样时间最多超过效果时长 2 秒');
      if (o.seed !== undefined) doc.seed = o.seed;
      job.docHash = await sha256(doc);
      job.snapshotId = await sha256({
        doc,
        scene: o.scene ?? 'dark_ground',
        camera: o.camera ?? 'three_quarter',
        times,
        width,
        height,
        renderer: 'three-0.180.0-webgl2',
      });
      const canvas = document.createElement('canvas');
      renderer = new THREE.WebGLRenderer({
        canvas,
        antialias: false,
        preserveDrawingBuffer: true,
        powerPreference: 'high-performance',
      });
      renderer.setPixelRatio(1);
      renderer.setSize(width, height, false);
      const renderingErrors: Diagnostic[] = [];
      renderer.debug.onShaderError = (gl, p, vs, fs) =>
        renderingErrors.push({
          code: 'PREVIEW_SHADER_ERROR',
          severity: 'error',
          message: [gl.getProgramInfoLog(p), gl.getShaderInfoLog(vs), gl.getShaderInfoLog(fs)]
            .filter(Boolean)
            .join('\n'),
        });
      const scene = new THREE.Scene(),
        camera = new THREE.PerspectiveCamera(43, width / height, 0.05, 150);
      if (o.camera === 'front') camera.position.set(0, 2.2, 9);
      else if (o.camera === 'top') {
        camera.position.set(0.01, 9, 0.01);
        camera.up.set(0, 0, -1);
      } else camera.position.set(6, 4.1, 7.6);
      camera.lookAt(0, o.camera === 'top' ? 0 : 1, 0);
      camera.updateMatrixWorld();
      stage = createStage(
        scene,
        o.scene === 'light_ground' ? 'light' : o.scene === 'arena' ? 'arena' : 'dark',
      );
      runtime = new VfxRuntime(scene, camera, renderer);
      pipeline = new VfxRenderer(renderer, runtime);
      pipeline.setSize(width, height);
      job.progress = '准备资源与编译 Shader';
      await runtime.register(doc);
      check();
      const instance = runtime.play(doc.id, { loop: false });
      const gl = renderer.getContext(),
        debug = gl.getExtension('WEBGL_debug_renderer_info');
      job.rendererInfo = {
        engine: 'three.js 0.180.0',
        backend: 'WebGL2',
        gpu: debug ? gl.getParameter(debug.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER),
        userAgent: navigator.userAgent,
        width,
        height,
        devicePixelRatio: 1,
        warmupFrames: 3,
      };
      for (let i = 0; i < 3; i++) {
        instance.seek(times[0]);
        pipeline.render();
        await nextFrame();
        check();
      }
      const outputs = o.outputs ?? ['contactSheet', 'metrics'];
      const sheets = document.createElement('canvas'),
        cols = Math.min(3, times.length),
        thumbWidth = 320,
        thumbHeight = Math.round((height / width) * thumbWidth),
        caption = 25;
      sheets.width = cols * thumbWidth;
      sheets.height = Math.ceil(times.length / cols) * (thumbHeight + caption);
      const ctx = sheets.getContext('2d')!;
      ctx.fillStyle = '#12161c';
      ctx.fillRect(0, 0, sheets.width, sheets.height);
      const sampleMetrics: { time: number; metrics: ReturnType<VfxRenderer['getMetrics']> }[] = [];
      for (let i = 0; i < times.length; i++) {
        check();
        job.progress = `渲染采样帧 ${i + 1}/${times.length}`;
        instance.seek(times[i]);
        pipeline.render();
        if (renderingErrors.length) throw new VfxValidationError(renderingErrors);
        await nextFrame();
        sampleMetrics.push({ time: times[i], metrics: pipeline.getMetrics() });
        const x = (i % cols) * thumbWidth,
          y = Math.floor(i / cols) * (thumbHeight + caption);
        ctx.drawImage(canvas, x, y, thumbWidth, thumbHeight);
        ctx.fillStyle = '#aab4c2';
        ctx.font = '12px ui-monospace,monospace';
        ctx.fillText(`${times[i].toFixed(2)} s  |  r${job.revision}`, x + 10, y + thumbHeight + 17);
        if (outputs.includes('frames'))
          job.artifacts.push({
            artifactId: uid('frame'),
            kind: 'frame',
            mimeType: 'image/png',
            filename: `frame-${i}.png`,
            dataBase64: canvas.toDataURL('image/png').split(',')[1],
            times: [times[i]],
          });
      }
      await nextFrame();
      job.metrics = pipeline.getMetrics();
      job.sampleMetrics = sampleMetrics;
      job.metricsAggregation = 'maxOfSampleFrames';
      for (const key of [
        'activeInstances',
        'visibleLayers',
        'particleSlots',
        'activeParticles',
        'drawCalls',
        'triangles',
        'cpuSubmitMs',
        'passes',
        'droppedCustomPasses',
      ] as const)
        job.metrics[key] = Math.max(...sampleMetrics.map((s) => s.metrics[key]));
      const gpu = sampleMetrics.map((s) => s.metrics.gpuMs).filter((n): n is number => n !== null);
      if (gpu.length) {
        job.metrics.gpuMs = Math.max(...gpu);
        job.metrics.gpuUnavailableReason = null;
      }
      if (outputs.includes('contactSheet'))
        job.artifacts.unshift({
          artifactId: uid('sheet'),
          kind: 'contactSheet',
          mimeType: 'image/png',
          filename: 'contact-sheet.png',
          dataBase64: sheets.toDataURL('image/png').split(',')[1],
          times,
        });
      if (outputs.includes('video')) {
        if (typeof MediaRecorder === 'undefined' || !MediaRecorder.isTypeSupported('video/webm'))
          job.diagnostics.push({
            code: 'VIDEO_UNSUPPORTED',
            severity: 'warning',
            message: '当前浏览器不支持 WebM 录制，其他产物已生成',
          });
        else {
          job.progress = '录制 WebM 预览';
          const stream = canvas.captureStream(30),
            recorder = new MediaRecorder(stream, {
              mimeType: 'video/webm',
              videoBitsPerSecond: 3000000,
            }),
            parts: BlobPart[] = [];
          recorder.ondataavailable = (e) => {
            if (e.data.size) parts.push(e.data);
          };
          const stopped = new Promise<void>((resolve) => {
            recorder.onstop = () => resolve();
          });
          recorder.start();
          const start = performance.now(),
            duration = Math.min(6, doc.duration);
          while ((performance.now() - start) / 1000 < duration && !job.cancelled) {
            instance.seek(Math.min(duration, (performance.now() - start) / 1000));
            pipeline.render();
            await nextFrame();
          }
          recorder.stop();
          await stopped;
          stream.getTracks().forEach((t) => t.stop());
          check();
          const data = new Uint8Array(await new Blob(parts, { type: 'video/webm' }).arrayBuffer());
          job.artifacts.push({
            artifactId: uid('video'),
            kind: 'video',
            mimeType: 'video/webm',
            filename: 'preview.webm',
            dataBase64: bytesToBase64(data),
          });
        }
      }
      job.status = 'succeeded';
      job.progress = '预览完成';
    } catch (e) {
      if (job.cancelled || (e instanceof Error && e.message === 'JOB_CANCELLED')) {
        job.status = 'cancelled';
        job.progress = '已取消';
      } else {
        job.status = 'failed';
        job.progress = '预览失败';
        job.diagnostics.push(
          ...(e instanceof VfxValidationError
            ? e.diagnostics
            : [
                {
                  code: 'PREVIEW_ERROR',
                  severity: 'error' as const,
                  message: e instanceof Error ? e.message : String(e),
                },
              ]),
        );
      }
    } finally {
      stage?.dispose();
      runtime?.dispose();
      pipeline?.dispose();
      renderer?.dispose();
      renderer?.forceContextLoss();
    }
  }
  dispose() {
    for (const j of this.jobs.values()) j.cancelled = true;
  }
}
