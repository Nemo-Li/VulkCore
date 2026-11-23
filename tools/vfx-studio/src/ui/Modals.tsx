import { useEffect, useRef, useState, type ReactNode } from 'react';
import JSZip from 'jszip';
import {
  X,
  Play,
  CheckCircle2,
  AlertTriangle,
  Download,
  Upload,
  Braces,
  Loader2,
} from 'lucide-react';
import { uid, shaderSchema, type ShaderAsset, type Diagnostic } from '../core/model';
import { EditorStore, type Command } from '../core/store';
import { EditorEngine } from '../editor/engine';
import { downloadBlob } from '../editor/persistence';
import {
  vertexMesh,
  vertexParticle,
  vertexPost,
  fragmentVfx,
  fragmentPost,
} from '../runtime/shaders';
import { apply, report } from './controls';
export function Modal({
  title,
  kicker,
  children,
  onClose,
  wide = false,
}: {
  title: string;
  kicker?: string;
  children: ReactNode;
  onClose: () => void;
  wide?: boolean;
}) {
  useEffect(() => {
    const key = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onClose();
    };
    window.addEventListener('keydown', key);
    return () => window.removeEventListener('keydown', key);
  }, [onClose]);
  return (
    <div
      className="modal-backdrop"
      onMouseDown={(e) => {
        if (e.target === e.currentTarget) onClose();
      }}
    >
      <div
        className={`modal ${wide ? 'wide' : ''}`}
        role="dialog"
        aria-modal="true"
        aria-label={title}
      >
        <div className="modal-header">
          <div>
            {kicker && <small>{kicker}</small>}
            <h2>{title}</h2>
          </div>
          <button className="icon-button" aria-label="关闭窗口" onClick={onClose}>
            <X size={18} />
          </button>
        </div>
        {children}
      </div>
    </div>
  );
}
function template(
  profile: ShaderAsset['profile'],
): Pick<ShaderAsset, 'vertex' | 'fragment' | 'uniforms'> {
  const vertex =
    profile === 'particle' ? vertexParticle : profile === 'post' ? vertexPost : vertexMesh;
  const raw = profile === 'post' ? fragmentPost : fragmentVfx;
  let fragment = raw.replace('out vec4 outColor;', 'uniform float uGain; out vec4 outColor;');
  fragment =
    profile === 'post'
      ? fragment.replace('color+uTint*ring*uStrength', '(color+uTint*ring*uStrength)*uGain')
      : fragment.replace('col*uIntensity*texel.rgb', 'col*uIntensity*uGain*texel.rgb');
  return {
    vertex,
    fragment,
    uniforms: [{ name: 'uGain', type: 'float', default: 1, min: 0, max: 10, label: '增益' }],
  };
}
export function ShaderWorkbench({
  store,
  engine,
  initialId,
  onClose,
}: {
  store: EditorStore;
  engine: EditorEngine;
  initialId?: string;
  onClose: () => void;
}) {
  const doc = store.state.doc,
    layer = doc.layers.find((l) => l.id === store.state.selectedId),
    existing = doc.shaders.find(
      (s) =>
        s.id ===
        (initialId ??
          (layer?.kind === 'postprocess' ? layer.post.shaderId : layer?.material.shaderId)),
    );
  const profile =
    existing?.profile ??
    (layer?.kind === 'particles' ? 'particle' : layer?.kind === 'postprocess' ? 'post' : 'mesh');
  const [draft, setDraft] = useState<ShaderAsset>(() =>
      existing
        ? structuredClone(existing)
        : { id: uid('shader'), name: '自定义发光', profile, ...template(profile) },
    ),
    [meta, setMeta] = useState(() =>
      JSON.stringify(existing?.uniforms ?? template(profile).uniforms, null, 2),
    ),
    [tab, setTab] = useState<'fragment' | 'vertex' | 'metadata'>('fragment'),
    [busy, setBusy] = useState(false),
    [diagnostics, setDiagnostics] = useState<Diagnostic[]>([]),
    [compiled, setCompiled] = useState(false),
    [live, setLive] = useState(false);
  const input = useRef<HTMLInputElement>(null),
    lineNumbers = useRef<HTMLDivElement>(null),
    textarea = useRef<HTMLTextAreaElement>(null),
    compileTicket = useRef(0),
    draftBase = useRef({
      effectId: doc.id,
      layerId: layer?.id,
      shader: existing ? structuredClone(existing) : undefined,
    });
  const source = tab === 'metadata' ? meta : draft[tab];
  const edit = (text: string) => {
    setCompiled(false);
    if (tab === 'metadata') setMeta(text);
    else setDraft((d) => ({ ...d, [tab]: text }));
  };
  async function compile(bind = false) {
    const ticket = ++compileTicket.current,
      base = store.state;
    setBusy(true);
    try {
      if (
        bind &&
        (base.doc.id !== draftBase.current.effectId ||
          JSON.stringify(base.doc.shaders.find((s) => s.id === draft.id) ?? null) !==
            JSON.stringify(draftBase.current.shader ?? null))
      )
        throw new Error(
          '效果或 Shader 已由其他操作更新；当前草稿已保留，请重新打开工作台读取最新版本。',
        );
      const shader = shaderSchema.parse({ ...draft, uniforms: JSON.parse(meta) }),
        candidate = structuredClone(base.doc),
        i = candidate.shaders.findIndex((s) => s.id === shader.id);
      if (i < 0) candidate.shaders.push(shader);
      else candidate.shaders[i] = shader;
      const result = await engine.runtime.validate(candidate);
      if (ticket !== compileTicket.current) return;
      setDiagnostics(result);
      const ok = !result.some((d) => d.severity === 'error');
      setCompiled(ok);
      if (ok && bind) {
        const selected = candidate.layers.find((l) => l.id === draftBase.current.layerId),
          commands: Command[] = [{ type: 'upsertShader', shader }];
        const expected =
          selected?.kind === 'particles'
            ? 'particle'
            : selected?.kind === 'postprocess'
              ? 'post'
              : 'mesh';
        if (selected && selected.kind !== 'light' && expected === shader.profile) {
          if (selected.kind === 'postprocess')
            commands.push(
              { type: 'setParameter', targetId: selected.id, name: 'post.mode', value: 'custom' },
              {
                type: 'setParameter',
                targetId: selected.id,
                name: 'post.shaderId',
                value: shader.id,
              },
            );
          else
            commands.push({
              type: 'setParameter',
              targetId: selected.id,
              name: 'material.shaderId',
              value: shader.id,
            });
        }
        store.apply(commands, {
          effectId: base.doc.id,
          expectedRevision: base.revision,
          label: '应用自定义 Shader',
          group: live ? 'shader-live-' + shader.id : undefined,
        });
        draftBase.current.shader = structuredClone(shader);
        store.status(
          selected && selected.kind !== 'light' && expected === shader.profile
            ? 'Shader 已编译并应用'
            : 'Shader 已编译并加入资源库',
        );
      }
    } catch (e) {
      if (ticket !== compileTicket.current) return;
      setDiagnostics([
        {
          code: 'SHADER_INPUT_ERROR',
          severity: 'error',
          message: e instanceof Error ? e.message : String(e),
        },
      ]);
      setCompiled(false);
    } finally {
      if (ticket === compileTicket.current) setBusy(false);
    }
  }
  useEffect(() => {
    if (!live) return;
    const timer = setTimeout(() => void compile(true), 850);
    return () => clearTimeout(timer);
  }, [draft.vertex, draft.fragment, meta, live]);
  async function importFiles(files: FileList | null) {
    if (!files) return;
    try {
      let next = { ...draft },
        uniforms: unknown = JSON.parse(meta);
      for (const f of Array.from(files)) {
        const text = await f.text(),
          name = f.name.toLowerCase();
        if (name.endsWith('.json')) {
          const m = JSON.parse(text);
          if (m.name) next.name = m.name;
          if (['particle', 'mesh', 'post'].includes(m.profile)) next.profile = m.profile;
          if (m.type === 'postprocess') next.profile = 'post';
          if (m.uniforms)
            uniforms = Array.isArray(m.uniforms)
              ? m.uniforms
              : Object.entries(m.uniforms).map(([name, v]) => ({ name, ...(v as object) }));
        } else if (/\.vert(?:\.glsl)?$|\.vs$/i.test(name)) next.vertex = text;
        else next.fragment = text;
      }
      setDraft(next);
      setMeta(JSON.stringify(uniforms, null, 2));
      setCompiled(false);
      setDiagnostics([]);
    } catch (e) {
      report(store, e);
    }
  }
  async function download() {
    try {
      const shader = shaderSchema.parse({ ...draft, uniforms: JSON.parse(meta) }),
        zip = new JSZip();
      zip.file('shader.vert.glsl', shader.vertex);
      zip.file('shader.frag.glsl', shader.fragment);
      zip.file(
        'material.json',
        JSON.stringify(
          { name: shader.name, profile: shader.profile, uniforms: shader.uniforms },
          null,
          2,
        ),
      );
      downloadBlob(await zip.generateAsync({ type: 'blob' }), shader.name + '.shader.zip');
    } catch (e) {
      report(store, e);
    }
  }
  return (
    <Modal title="Shader 工作台" kicker="CUSTOM GLSL / LIVE COMPILE" wide onClose={onClose}>
      <div className="shader-toolbar">
        <Braces size={16} />
        <input
          aria-label="Shader 名称"
          value={draft.name}
          onChange={(e) => setDraft({ ...draft, name: e.target.value })}
        />
        <select
          aria-label="Shader 协议"
          value={draft.profile}
          onChange={(e) => {
            const p = e.target.value as ShaderAsset['profile'];
            setDraft({ ...draft, profile: p, ...template(p) });
            setMeta(JSON.stringify(template(p).uniforms, null, 2));
            setCompiled(false);
          }}
        >
          <option value="particle">粒子 / Particle</option>
          <option value="mesh">网格与拖尾 / Mesh</option>
          <option value="post">后处理 / Post</option>
        </select>
        <button className="secondary-button" onClick={() => input.current?.click()}>
          <Upload size={13} /> 导入文件
        </button>
        <button className="secondary-button" onClick={download}>
          <Download size={13} /> 源码包
        </button>
        <input
          ref={input}
          type="file"
          hidden
          multiple
          accept=".glsl,.vert,.frag,.vs,.fs,.json"
          onChange={(e) => void importFiles(e.target.files)}
        />
      </div>
      <div className="code-tabs">
        {[
          ['fragment', '片元 · frag.glsl'],
          ['vertex', '顶点 · vert.glsl'],
          ['metadata', '参数 · material.json'],
        ].map(([id, label]) => (
          <button
            key={id}
            className={tab === id ? 'selected' : ''}
            onClick={() => setTab(id as typeof tab)}
          >
            {label}
          </button>
        ))}
        <span>GLSL 3 · 线性颜色 · Y UP</span>
      </div>
      <div className="shader-code">
        <div className="line-numbers" ref={lineNumbers}>
          {source.split('\n').map((_, i) => (
            <span className={diagnostics.some((d) => d.line === i + 1) ? 'line-error' : ''} key={i}>
              {i + 1}
            </span>
          ))}
        </div>
        <textarea
          ref={textarea}
          aria-label="Shader 源代码"
          value={source}
          spellCheck={false}
          wrap="off"
          onChange={(e) => edit(e.target.value)}
          onScroll={(e) => {
            if (lineNumbers.current) lineNumbers.current.scrollTop = e.currentTarget.scrollTop;
          }}
          onKeyDown={(e) => {
            if (e.key === 'Tab') {
              e.preventDefault();
              const pos = e.currentTarget.selectionStart,
                end = e.currentTarget.selectionEnd;
              edit(source.slice(0, pos) + '  ' + source.slice(end));
              requestAnimationFrame(() => textarea.current?.setSelectionRange(pos + 2, pos + 2));
            }
          }}
        />
      </div>
      <div className="shader-diagnostics">
        {busy ? (
          <span>
            <Loader2 size={14} className="spin" /> 正在实际编译…
          </span>
        ) : diagnostics.length ? (
          diagnostics.map((d, i) => (
            <p key={i}>
              <AlertTriangle size={13} />
              {d.file && `${d.file}:${d.line ?? '?'}`} {d.message}
            </p>
          ))
        ) : compiled ? (
          <span className="success-text">
            <CheckCircle2 size={14} /> 编译通过，应用后可在场景中检查实际表现
          </span>
        ) : (
          <span>
            片元、顶点源码与参数元数据共同构成可移植材质。内置时间、相机、深度与几何协议由运行时提供。
          </span>
        )}
      </div>
      <div className="modal-footer">
        <label className="inline-check">
          <input type="checkbox" checked={live} onChange={(e) => setLive(e.target.checked)} />{' '}
          成功后实时应用
        </label>
        <span className="muted-text">失败保留最后有效画面</span>
        <button className="secondary-button" disabled={busy} onClick={() => void compile(false)}>
          试编译
        </button>
        <button className="primary-button" disabled={busy} onClick={() => void compile(true)}>
          <Play size={13} /> 验证并应用
        </button>
      </div>
    </Modal>
  );
}
