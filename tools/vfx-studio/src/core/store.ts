import { z } from 'zod';
import {
  assetSchema,
  cloneDocument,
  curveSchema,
  documentSchema,
  getPath,
  layerSchema,
  makeLayer,
  parseDocument,
  setPath,
  shaderSchema,
  uid,
  type Diagnostic,
  type EffectDocument,
  type LayerKind,
} from './model';

export const commandSchema = z.discriminatedUnion('type', [
  z
    .object({
      type: z.literal('addLayer'),
      kind: z.enum(['particles', 'mesh', 'ribbon', 'light', 'postprocess']),
      id: z.string().optional(),
      name: z.string().optional(),
      layer: layerSchema.optional(),
    })
    .strict(),
  z.object({ type: z.literal('removeLayer'), targetId: z.string() }).strict(),
  z.object({ type: z.literal('duplicateLayer'), targetId: z.string() }).strict(),
  z
    .object({
      type: z.literal('reorderLayer'),
      targetId: z.string(),
      index: z.number().int().min(0),
    })
    .strict(),
  z
    .object({
      type: z.literal('setParameter'),
      targetId: z.string(),
      name: z.string(),
      value: z.unknown(),
    })
    .strict(),
  z
    .object({
      type: z.literal('setUniform'),
      targetId: z.string(),
      name: z.string(),
      value: z.unknown(),
    })
    .strict(),
  z
    .object({
      type: z.literal('setCurve'),
      targetId: z.string(),
      parameter: z.string(),
      curve: curveSchema.nullable(),
    })
    .strict(),
  z.object({ type: z.literal('setDocument'), name: z.string(), value: z.unknown() }).strict(),
  z.object({ type: z.literal('resizeDuration'), duration: z.number().min(0.1).max(120) }).strict(),
  z.object({ type: z.literal('upsertShader'), shader: shaderSchema }).strict(),
  z.object({ type: z.literal('addAsset'), asset: assetSchema }).strict(),
  z.object({ type: z.literal('removeAsset'), targetId: z.string() }).strict(),
  z.object({ type: z.literal('removeShader'), targetId: z.string() }).strict(),
]);
export type Command = z.infer<typeof commandSchema>;
export class EditorError extends Error {
  constructor(
    public code: string,
    message: string,
    public details?: unknown,
  ) {
    super(message);
  }
}
type HistoryEntry = {
  before: EffectDocument;
  after: EffectDocument;
  label: string;
  transactionId: string;
  group?: string;
  timestamp?: number;
};
export type EditResult = {
  ok: true;
  revision: number;
  transactionId: string;
  label: string;
  changes: string[];
  replayed?: boolean;
  currentRevision?: number;
};
export type EditorState = {
  doc: EffectDocument;
  revision: number;
  selectedId: string | null;
  diagnostics: Diagnostic[];
  undoCount: number;
  redoCount: number;
  saved: boolean;
  status: string;
};
export class EditorStore {
  private listeners = new Set<() => void>();
  private undoStack: HistoryEntry[] = [];
  private redoStack: HistoryEntry[] = [];
  private requests = new Map<string, { payload: string; result: EditResult }>();
  state: EditorState;
  playback = { time: 0.48, playing: true, speed: 1, loop: true };
  soloId: string | null = null;
  constructor(doc: EffectDocument) {
    this.state = {
      doc: parseDocument(doc),
      revision: 1,
      selectedId: doc.layers[0]?.id ?? null,
      diagnostics: [],
      undoCount: 0,
      redoCount: 0,
      saved: false,
      status: '就绪',
    };
    this.playback.loop = doc.loop;
  }
  subscribe = (fn: () => void) => {
    this.listeners.add(fn);
    return () => this.listeners.delete(fn);
  };
  getSnapshot = () => this.state;
  private emit(patch: Partial<EditorState> = {}) {
    this.state = { ...this.state, ...patch };
    for (const fn of this.listeners) fn();
  }
  select(id: string | null) {
    this.emit({ selectedId: id });
  }
  status(status: string) {
    this.emit({ status });
  }
  diagnostics(diagnostics: Diagnostic[], revision = this.state.revision) {
    if (revision === this.state.revision) this.emit({ diagnostics });
  }
  markSaved(revision: number) {
    if (revision === this.state.revision) this.emit({ saved: true });
  }
  replace(input: unknown, label = '打开项目') {
    const doc = parseDocument(input);
    this.undoStack = [];
    this.redoStack = [];
    this.requests.clear();
    this.soloId = null;
    this.playback.time = Math.min(0.48, doc.duration * 0.25);
    this.playback.loop = doc.loop;
    this.emit({
      doc,
      revision: this.state.revision + 1,
      selectedId: doc.layers[0]?.id ?? null,
      diagnostics: [],
      undoCount: 0,
      redoCount: 0,
      saved: false,
      status: label,
    });
  }
  apply(
    rawCommands: unknown[],
    options: {
      expectedRevision?: number;
      requestId?: string;
      label?: string;
      effectId?: string;
      group?: string;
    } = {},
  ): EditResult {
    const payload = JSON.stringify({
      rawCommands,
      expectedRevision: options.expectedRevision,
      effectId: options.effectId,
    });
    if (options.requestId && this.requests.has(options.requestId)) {
      const cached = this.requests.get(options.requestId)!;
      if (cached.payload !== payload)
        throw new EditorError('IDEMPOTENCY_CONFLICT', '相同 requestId 对应不同负载');
      return { ...cached.result, replayed: true, currentRevision: this.state.revision };
    }
    if (options.effectId && options.effectId !== this.state.doc.id)
      throw new EditorError('EFFECT_NOT_OPEN', '目标效果未在当前编辑会话打开', {
        activeEffectId: this.state.doc.id,
      });
    if (options.expectedRevision !== undefined && options.expectedRevision !== this.state.revision)
      throw new EditorError('REVISION_CONFLICT', '效果已被修改，请重新查询上下文', {
        currentRevision: this.state.revision,
      });
    if (!rawCommands.length || rawCommands.length > 200)
      throw new EditorError('INVALID_COMMANDS', '事务应包含 1–200 条命令');
    const commands = rawCommands.map((c) => commandSchema.parse(c));
    const doc = cloneDocument(this.state.doc);
    const changes: string[] = [];
    for (const c of commands) {
      if (c.type === 'addLayer') {
        const l = c.layer ?? makeLayer(c.kind as LayerKind);
        if (c.id) l.id = c.id;
        if (c.name) l.name = c.name;
        l.duration = Math.min(l.duration, doc.duration - l.start);
        doc.layers.push(l);
        changes.push(`添加 ${l.name}`);
      } else if (c.type === 'setDocument') {
        if (
          !['name', 'description', 'duration', 'loop', 'seed', 'settings'].includes(
            c.name.split('.')[0],
          )
        )
          throw new EditorError('INVALID_DOCUMENT_PATH', '此字段需要专用编辑命令');
        setPath(doc, c.name, c.value);
        changes.push(`修改 ${c.name}`);
      } else if (c.type === 'resizeDuration') {
        const factor = c.duration / doc.duration;
        doc.duration = c.duration;
        for (const l of doc.layers) {
          l.start = Math.min(c.duration - 0.02, l.start * factor);
          l.duration = Math.max(0.02, Math.min(c.duration - l.start, l.duration * factor));
          l.emitter.lifetime = Math.max(0.02, Math.min(30, l.emitter.lifetime * factor));
          l.emitter.emissionWindow = Math.min(120, l.emitter.emissionWindow * factor);
          l.emitter.rate = Math.max(1, Math.min(2000, l.emitter.rate / factor));
          for (const cv of Object.values(l.curves))
            if (cv.domain === 'layer') for (const k of cv.keys) k.time *= factor;
        }
        changes.push('缩放效果时间');
      } else if (c.type === 'upsertShader') {
        const i = doc.shaders.findIndex((s) => s.id === c.shader.id);
        if (i < 0) doc.shaders.push(c.shader);
        else doc.shaders[i] = c.shader;
        changes.push(`Shader ${c.shader.name}`);
      } else if (c.type === 'addAsset') {
        const i = doc.assets.findIndex((a) => a.id === c.asset.id);
        if (i < 0) doc.assets.push(c.asset);
        else doc.assets[i] = c.asset;
        changes.push(`资源 ${c.asset.name}`);
      } else if (c.type === 'removeShader') {
        doc.shaders = doc.shaders.filter((s) => s.id !== c.targetId);
        for (const l of doc.layers) {
          if (l.material.shaderId === c.targetId) {
            l.material.shaderId = '';
            l.material.uniforms = {};
            for (const path of Object.keys(l.curves))
              if (path.startsWith('uniforms.')) delete l.curves[path];
          }
          if (l.post.shaderId === c.targetId) {
            l.post.shaderId = '';
            l.post.mode = 'distortion';
            l.material.uniforms = {};
            for (const path of Object.keys(l.curves))
              if (path.startsWith('uniforms.')) delete l.curves[path];
          }
        }
        changes.push('删除 Shader 并解除绑定');
      } else if (c.type === 'removeAsset') {
        doc.assets = doc.assets.filter((a) => a.id !== c.targetId);
        changes.push('删除资源');
      } else {
        const i = doc.layers.findIndex((l) => l.id === c.targetId);
        if (i < 0) throw new EditorError('TARGET_NOT_FOUND', `图层不存在：${c.targetId}`);
        const l = doc.layers[i];
        if (c.type === 'removeLayer') {
          doc.layers.splice(i, 1);
          changes.push(`删除 ${l.name}`);
        } else if (c.type === 'duplicateLayer') {
          const copy = structuredClone(l);
          copy.id = uid('layer');
          copy.name += ' 副本';
          doc.layers.splice(i + 1, 0, copy);
          changes.push(`复制 ${l.name}`);
        } else if (c.type === 'reorderLayer') {
          doc.layers.splice(i, 1);
          doc.layers.splice(Math.min(c.index, doc.layers.length), 0, l);
          changes.push(`排序 ${l.name}`);
        } else if (c.type === 'setParameter') {
          if (['id', 'kind'].includes(c.name.split('.')[0]))
            throw new EditorError('IMMUTABLE_ID', '图层 ID 与类型由添加命令确定');
          if (
            ['material.shaderId', 'post.shaderId'].includes(c.name) &&
            getPath(l, c.name) !== c.value
          ) {
            l.material.uniforms = {};
            for (const key of Object.keys(l.curves))
              if (key.startsWith('uniforms.')) delete l.curves[key];
          }
          setPath(l, c.name, c.value);
          changes.push(`${l.name}.${c.name}`);
        } else if (c.type === 'setCurve') {
          if (
            !c.parameter.startsWith('life.') &&
            !c.parameter.startsWith('uniforms.') &&
            typeof getPath(l, c.parameter) !== 'number'
          )
            throw new EditorError('INVALID_CURVE_TARGET', '曲线仅绑定数值参数');
          if (c.curve) l.curves[c.parameter] = c.curve;
          else delete l.curves[c.parameter];
          changes.push(`曲线 ${c.parameter}`);
        } else if (c.type === 'setUniform') {
          const builtins: Record<string, string> = {
            uIntensity: 'intensity',
            uOpacity: 'opacity',
            uTint: 'color',
          };
          if (builtins[c.name]) setPath(l.material, builtins[c.name], c.value);
          else {
            const shader = doc.shaders.find(
              (s) => s.id === (l.kind === 'postprocess' ? l.post.shaderId : l.material.shaderId),
            );
            const def = shader?.uniforms.find((u) => u.name === c.name);
            if (!def) throw new EditorError('UNIFORM_NOT_FOUND', `未声明参数 ${c.name}`);
            if (
              def.type === 'float' &&
              (typeof c.value !== 'number' ||
                c.value < (def.min ?? -Infinity) ||
                c.value > (def.max ?? Infinity))
            )
              throw new EditorError('UNIFORM_OUT_OF_RANGE', 'Uniform 数值不符合描述');
            l.material.uniforms[c.name] = c.value as LayerUniformValue;
          }
          changes.push(`Uniform ${c.name}`);
        }
      }
    }
    const parsed = parseDocument(doc);
    let transactionId = uid('tx');
    const label = options.label ?? changes[0];
    const previous = this.undoStack.at(-1),
      now = Date.now();
    if (
      options.group &&
      !options.requestId &&
      previous?.group === options.group &&
      now - (previous.timestamp ?? 0) < 600
    ) {
      previous.after = parsed;
      previous.label = label;
      previous.timestamp = now;
      transactionId = previous.transactionId;
    } else
      this.undoStack.push({
        before: this.state.doc,
        after: parsed,
        transactionId,
        label,
        group: options.group,
        timestamp: now,
      });
    if (this.undoStack.length > 80) this.undoStack.shift();
    this.redoStack = [];
    const selectedId = parsed.layers.some((l) => l.id === this.state.selectedId)
      ? this.state.selectedId
      : (parsed.layers[0]?.id ?? null);
    this.emit({
      doc: parsed,
      revision: this.state.revision + 1,
      selectedId,
      undoCount: this.undoStack.length,
      redoCount: 0,
      saved: false,
      status: label,
      diagnostics: [],
    });
    const result: EditResult = {
      ok: true,
      revision: this.state.revision,
      transactionId,
      label,
      changes,
    };
    if (options.requestId) {
      this.requests.set(options.requestId, { payload, result });
      if (this.requests.size > 200) this.requests.delete(this.requests.keys().next().value!);
    }
    return result;
  }
  undo(expectedRevision?: number, transactionId?: string) {
    if (expectedRevision !== undefined && expectedRevision !== this.state.revision)
      throw new EditorError('REVISION_CONFLICT', '撤销目标版本已变化');
    const entry = this.undoStack.at(-1);
    if (!entry) throw new EditorError('NOTHING_TO_UNDO', '没有可撤销的操作');
    if (transactionId && transactionId !== entry.transactionId)
      throw new EditorError('HISTORY_CONFLICT', '目标事务之后仍有编辑，请先检查历史');
    this.undoStack.pop();
    this.redoStack.push(entry);
    this.emit({
      doc: entry.before,
      selectedId: entry.before.layers.some((l) => l.id === this.state.selectedId)
        ? this.state.selectedId
        : (entry.before.layers[0]?.id ?? null),
      revision: this.state.revision + 1,
      undoCount: this.undoStack.length,
      redoCount: this.redoStack.length,
      saved: false,
      status: `撤销：${entry.label}`,
      diagnostics: [],
    });
    return { ok: true, revision: this.state.revision };
  }
  redo() {
    const e = this.redoStack.pop();
    if (!e) return;
    this.undoStack.push(e);
    this.emit({
      doc: e.after,
      selectedId: e.after.layers.some((l) => l.id === this.state.selectedId)
        ? this.state.selectedId
        : (e.after.layers[0]?.id ?? null),
      revision: this.state.revision + 1,
      undoCount: this.undoStack.length,
      redoCount: this.redoStack.length,
      saved: false,
      status: `重做：${e.label}`,
      diagnostics: [],
    });
  }
  history() {
    return this.undoStack.map(({ label, transactionId }) => ({ label, transactionId }));
  }
  schema() {
    return documentSchema;
  }
}
type LayerUniformValue = EffectDocument['layers'][number]['material']['uniforms'][string];
