import { useEffect, useRef, useState } from 'react';
import {
  ArrowUpRight,
  Undo2,
  Redo2,
  Save,
  FolderOpen,
  FilePlus2,
  ChevronDown,
  Download,
  Hand,
  Move,
  Rotate3D,
  Scaling,
  Grid2X2,
  UserRound,
  Focus,
  Braces,
  HelpCircle,
  Check,
  Loader2,
  TriangleAlert,
  ChevronRight,
} from 'lucide-react';
import { EditorStore, type Command as EditCommand } from './core/store';
import { createPreset, curve, PRESETS } from './core/presets';
import {
  getPath,
  makeDocument,
  makeLayer,
  shaderSchema,
  uid,
  type ShaderAsset,
} from './core/model';
import { EditorEngine, type EngineMetrics } from './editor/engine';
import { PreviewQueue } from './editor/preview';
import { exportNativeEffect } from './editor/native-effect';
import { fileAsset } from './runtime/assets';
import { exportPackage, freezeDocument, importProject, sha256 } from './editor/package';
import {
  saveProject,
  loadProject,
  downloadProject,
  downloadBlob,
  listProjects,
  type SavedProject,
} from './editor/persistence';
import { vertexMesh, vertexParticle, vertexPost } from './runtime/shaders';
import { Library } from './ui/Library';
import { Inspector } from './ui/Inspector';
import { Timeline, type BottomTab, usePlayhead } from './ui/Timeline';
import { Modal, ShaderWorkbench } from './ui/Modals';
import { IconButton, apply, report, useEditor } from './ui/controls';
import './style.css';
export default function App({ store }: { store: EditorStore }) {
  const state = useEditor(store),
    { doc, revision, diagnostics } = state;
  const canvas = useRef<HTMLCanvasElement>(null),
    engineRef = useRef<EditorEngine | null>(null),
    previewQueue = useRef<PreviewQueue | null>(null),
    openFile = useRef<HTMLInputElement>(null),
    assetFile = useRef<HTMLInputElement>(null);
  const [engine, setEngine] = useState<EditorEngine | null>(null),
    [metrics, setMetrics] = useState<EngineMetrics | null>(null),
    [tool, setTool] = useState<'orbit' | 'translate' | 'rotate' | 'scale'>('orbit'),
    [stage, setStage] = useState<'dark' | 'light' | 'arena'>('dark'),
    [grid, setGrid] = useState(true),
    [reference, setReference] = useState(false),
    [bottomTab, setBottomTab] = useState<BottomTab>('timeline'),
    [curveTarget, setCurveTarget] = useState('material.opacity'),
    [shaderOpen, setShaderOpen] = useState(false),
    [shaderId, setShaderId] = useState<string | undefined>(),
    [helpOpen, setHelpOpen] = useState(false),
    [projects, setProjects] = useState<SavedProject[] | null>(null),
    [exporting, setExporting] = useState<string | null>(null),
    [dropActive, setDropActive] = useState(false),
    [showInspector, setShowInspector] = useState(true);
  const exportTicket = useRef(0),
    exportJob = useRef<string | null>(null);
  useEffect(() => {
    let e: EditorEngine;
    try {
      e = new EditorEngine(canvas.current!, store, setMetrics);
    } catch (error) {
      report(store, error);
      return;
    }
    const previews = new PreviewQueue();
    engineRef.current = e;
    previewQueue.current = previews;
    setEngine(e);
    window.__IGNIS__ = {
      store,
      engine: e,
      ready: false,
      setTime(t) {
        store.playback.playing = false;
        store.playback.time = t;
      },
      async loadPreset(id) {
        await saveProject(store.state.doc);
        store.replace(createPreset(id), '加载预设');
        await e.whenSynchronized();
      },
      getDocument: () => structuredClone(store.state.doc),
      getMetrics: () => e.getMetrics(),
    };
    e.whenSynchronized().then(() => {
      if (window.__IGNIS__) window.__IGNIS__.ready = e.lastGoodRevision > 0;
    });
    return () => {
      exportTicket.current++;
      if (exportJob.current) previews.cancel(exportJob.current);
      previewQueue.current = null;
      e.dispose();
      delete window.__IGNIS__;
    };
  }, [store]);
  useEffect(() => {
    if (state.saved) return;
    const version = revision,
      t = setTimeout(() => {
        saveProject(doc)
          .then(() => store.markSaved(version))
          .catch((e) => report(store, e));
      }, 650);
    return () => clearTimeout(t);
  }, [doc, revision, state.saved]);
  async function save() {
    try {
      await saveProject(store.state.doc);
      store.markSaved(store.state.revision);
      downloadProject(store.state.doc);
      store.status('项目已保存到本机并下载 JSON');
    } catch (e) {
      report(store, e);
    }
  }
  async function preset(id: string) {
    try {
      await saveProject(store.state.doc);
      store.replace(createPreset(id), '加载 ' + (PRESETS.find((p) => p.id === id)?.name ?? id));
    } catch (e) {
      report(store, e);
    }
  }
  async function newProject() {
    try {
      await saveProject(store.state.doc);
      const d = makeDocument('未命名特效');
      d.layers = [makeLayer('particles')];
      store.replace(d, '新建项目');
    } catch (e) {
      report(store, e);
    }
  }
  function changeTool(mode: typeof tool) {
    setTool(mode);
    engineRef.current?.setTool(mode);
  }
  function openShader(id?: string) {
    setShaderId(id);
    setShaderOpen(true);
  }
  function openCurve(path: string) {
    const l = store.state.doc.layers.find((l) => l.id === store.state.selectedId);
    if (!l) return;
    setCurveTarget(path);
    setBottomTab('curves');
    if (!l.curves[path]) {
      const life = path.startsWith('life.'),
        value = life
          ? 1
          : path.startsWith('uniforms.')
            ? Number(
                l.material.uniforms[path.slice(9)] ??
                  store.state.doc.shaders
                    .find(
                      (s) =>
                        s.id === (l.kind === 'postprocess' ? l.post.shaderId : l.material.shaderId),
                    )
                    ?.uniforms.find((u) => u.name === path.slice(9))?.default ??
                  1,
              )
            : Number(getPath(l, path));
      if (Number.isFinite(value))
        apply(
          store,
          [
            {
              type: 'setCurve',
              targetId: l.id,
              parameter: path,
              curve: curve(
                [
                  [0, value],
                  [life ? 1 : l.duration, value],
                ],
                life ? 'life' : 'layer',
              ),
            },
          ],
          '添加参数曲线',
        );
    }
  }
  async function files(files: FileList | null) {
    if (!files?.length) return;
    try {
      const list = Array.from(files),
        first = list[0];
      if (
        /\.json$|\.zip$/i.test(first.name) &&
        !list.some((f) => /\.glsl$|\.vert$|\.frag$/i.test(f.name))
      ) {
        await saveProject(store.state.doc);
        store.replace(await importProject(first), '导入项目');
        setProjects(null);
        return;
      }
      if (list.some((f) => /\.glsl$|\.vert$|\.frag$/i.test(f.name))) {
        const selected = store.state.doc.layers.find((l) => l.id === store.state.selectedId),
          profile =
            selected?.kind === 'particles'
              ? 'particle'
              : selected?.kind === 'postprocess'
                ? 'post'
                : 'mesh';
        const s: ShaderAsset = {
          id: uid('shader'),
          name: first.name.replace(/\.(frag|vert)\.glsl$|\.glsl$/i, ''),
          profile,
          vertex:
            profile === 'particle' ? vertexParticle : profile === 'post' ? vertexPost : vertexMesh,
          fragment: '',
          uniforms: [],
        };
        for (const f of list) {
          const text = await f.text();
          if (/\.vert(?:\.glsl)?$/i.test(f.name)) s.vertex = text;
          else if (/\.json$/i.test(f.name)) {
            const m = JSON.parse(text);
            if (m.uniforms)
              s.uniforms = Array.isArray(m.uniforms)
                ? m.uniforms
                : Object.entries(m.uniforms).map(([name, v]) => ({ name, ...(v as object) }));
          } else s.fragment = text;
        }
        const shader = shaderSchema.parse(s),
          commands: EditCommand[] = [{ type: 'upsertShader', shader }];
        if (selected && selected.kind !== 'light') {
          if (selected.kind === 'postprocess')
            commands.push(
              { type: 'setParameter', targetId: selected.id, name: 'post.mode', value: 'custom' },
              { type: 'setParameter', targetId: selected.id, name: 'post.shaderId', value: s.id },
            );
          else
            commands.push({
              type: 'setParameter',
              targetId: selected.id,
              name: 'material.shaderId',
              value: s.id,
            });
        }
        apply(store, commands, '导入 Shader');
        openShader(s.id);
        return;
      }
      const assets = await Promise.all(list.map(fileAsset)),
        commands: EditCommand[] = assets.map((asset) => ({ type: 'addAsset', asset }));
      const selected = store.state.doc.layers.find((l) => l.id === store.state.selectedId);
      for (const asset of assets) {
        if (asset.type === 'texture' && selected)
          commands.push({
            type: 'setParameter',
            targetId: selected.id,
            name: 'material.textureId',
            value: asset.id,
          });
        if (asset.type === 'model') {
          const l = selected?.kind === 'mesh' ? structuredClone(selected) : makeLayer('mesh');
          l.mesh.shape = 'model';
          l.mesh.modelId = asset.id;
          l.mesh.radius = 1;
          l.duration = Math.min(l.duration, store.state.doc.duration);
          if (l.id === selected?.id)
            commands.push(
              { type: 'setParameter', targetId: l.id, name: 'mesh.shape', value: 'model' },
              { type: 'setParameter', targetId: l.id, name: 'mesh.modelId', value: asset.id },
              { type: 'setParameter', targetId: l.id, name: 'mesh.radius', value: 1 },
            );
          else commands.push({ type: 'addLayer', kind: 'mesh', layer: l });
        }
      }
      apply(store, commands, '导入资源');
      store.status(`已导入 ${assets.length} 个资源`);
    } catch (e) {
      report(store, e);
    }
  }
  async function exportEffect() {
    const currentEngine = engineRef.current;
    const previews = previewQueue.current;
    if (!currentEngine || !previews) return;
    const ticket = ++exportTicket.current;
    const { doc, revision } = store.state;
    const stillCurrent = () => {
      if (ticket !== exportTicket.current) return false;
      if (revision !== store.state.revision || doc.id !== store.state.doc.id) {
        throw new Error('导出期间项目已修改，请重新导出');
      }
      return true;
    };
    setExporting('验证素材与 Shader…');
    try {
      const snapshot = await freezeDocument(doc);
      const validation = await currentEngine.runtime.validate(snapshot);
      if (!stillCurrent()) return;
      if (validation.some((entry) => entry.severity === 'error')) {
        store.diagnostics(validation, revision);
        throw new Error('Shader 或素材验证失败');
      }
      setExporting('渲染预览…');
      const preview = previews.start(snapshot, revision, {
        times: [doc.duration * 0.12, doc.duration * 0.35, doc.duration + 0.1],
        outputs: ['contactSheet', 'metrics'],
      });
      exportJob.current = preview.jobId;
      const rendered = await previews.wait(preview.jobId);
      if (!stillCurrent()) return;
      if (rendered.status !== 'succeeded') {
        store.diagnostics(rendered.diagnostics, revision);
        throw new Error(rendered.diagnostics[0]?.message ?? '预览渲染失败');
      }
      setExporting('正在打包运行时与资源…');
      const result = await exportPackage(snapshot, {
        effectId: doc.id,
        revision,
        docHash: await sha256(snapshot),
        validation,
        preview: { snapshotId: rendered.snapshotId, metrics: rendered.metrics },
      });
      if (!stillCurrent()) return;
      downloadBlob(
        new Blob([Uint8Array.from(result.bytes)], { type: 'application/zip' }),
        result.filename,
      );
      store.status(`已导出版本 r${revision}，包含独立播放器和验证记录`);
    } catch (e) {
      if (ticket === exportTicket.current) {
        report(store, e);
        setBottomTab('diagnostics');
      }
    } finally {
      if (ticket === exportTicket.current) {
        setExporting(null);
        exportJob.current = null;
      }
    }
  }
  async function exportSceneEffect() {
    const preview = engineRef.current;
    if (!preview) return;
    const framing = {
      position: preview.camera.position.toArray(),
      target: preview.orbit.target.toArray(),
      fov: preview.camera.getEffectiveFOV(),
      aspect: preview.camera.aspect,
    };
    const ticket = ++exportTicket.current;
    const { doc, revision } = store.state;
    const assertCurrent = () => {
      if (ticket !== exportTicket.current) throw new Error('导出已取消');
      if (revision !== store.state.revision) throw new Error('导出期间项目已修改，请重新导出');
    };
    setExporting('正在编译 Vulkan 特效…');
    try {
      const result = await exportNativeEffect(doc, framing, assertCurrent);
      assertCurrent();
      downloadBlob(result.blob, result.filename);
      store.status('已导出实时特效，可在场景标签页导入 .vfx3d 文件');
    } catch (error) {
      if (ticket === exportTicket.current) report(store, error);
    } finally {
      if (ticket === exportTicket.current) setExporting(null);
    }
  }
  function cancelExport() {
    exportTicket.current++;
    const id = exportJob.current;
    if (id) previewQueue.current?.cancel(id);
    exportJob.current = null;
    setExporting(null);
    store.status('已取消导出');
  }
  useEffect(() => {
    const key = (e: KeyboardEvent) => {
      const target = e.target as HTMLElement;
      if (
        ['INPUT', 'TEXTAREA', 'SELECT'].includes(target.tagName) ||
        target.isContentEditable ||
        document.querySelector('[role="dialog"]')
      )
        return;
      const meta = e.metaKey || e.ctrlKey;
      if (meta && e.key.toLowerCase() === 's') {
        e.preventDefault();
        void save();
      } else if (meta && e.key.toLowerCase() === 'z') {
        e.preventDefault();
        if (e.shiftKey) store.redo();
        else if (store.state.undoCount) store.undo();
      } else if (meta && e.key.toLowerCase() === 'd') {
        e.preventDefault();
        if (store.state.selectedId)
          apply(store, [{ type: 'duplicateLayer', targetId: store.state.selectedId }], '复制图层');
      } else if (e.code === 'Space') {
        e.preventDefault();
        store.playback.playing = !store.playback.playing;
      } else if (['Delete', 'Backspace'].includes(e.key)) {
        e.preventDefault();
        if (store.state.selectedId)
          apply(store, [{ type: 'removeLayer', targetId: store.state.selectedId }], '删除图层');
      } else if (e.key.toLowerCase() === 'f') engineRef.current?.resetCamera();
      else if (e.key.toLowerCase() === 'w') changeTool('translate');
      else if (e.key.toLowerCase() === 'e') changeTool('rotate');
      else if (e.key.toLowerCase() === 'r') changeTool('scale');
      else if (e.key === 'Escape') changeTool('orbit');
    };
    window.addEventListener('keydown', key);
    return () => window.removeEventListener('keydown', key);
  }, [store]);
  return (
    <div
      className={`app ${!showInspector ? 'inspector-hidden' : ''}`}
      onDragOver={(e) => {
        if (e.dataTransfer.types.includes('Files')) e.preventDefault();
      }}
      onDragEnter={(e) => {
        if (e.dataTransfer.types.includes('Files')) setDropActive(true);
      }}
      onDragLeave={(e) => {
        if (!e.currentTarget.contains(e.relatedTarget as Node)) setDropActive(false);
      }}
      onDrop={(e) => {
        if (e.dataTransfer.files.length) {
          e.preventDefault();
          setDropActive(false);
          void files(e.dataTransfer.files);
        }
      }}
    >
      <header className="app-header">
        <div className="brand">
          <strong>特效制作</strong>
        </div>
        <div className="header-divider" />
        <button
          className="project-title"
          onClick={() =>
            void listProjects()
              .then(setProjects)
              .catch((e) => report(store, e))
          }
        >
          <span className="project-dot" />
          <span>{doc.name}</span>
          <ChevronDown size={12} />
          <small>项目</small>
        </button>
        <div className="save-indicator">
          {state.saved ? (
            <>
              <Check size={11} /> 已自动保存
            </>
          ) : (
            <>
              <span className="tiny-dot" /> 有新修改
            </>
          )}
        </div>
        <div className="header-actions">
          <IconButton label="撤销" disabled={!state.undoCount} onClick={() => store.undo()}>
            <Undo2 size={16} />
          </IconButton>
          <IconButton label="重做" disabled={!state.redoCount} onClick={() => store.redo()}>
            <Redo2 size={16} />
          </IconButton>
          <span className="action-divider" />
          <IconButton label="新建项目" onClick={() => void newProject()}>
            <FilePlus2 size={16} />
          </IconButton>
          <IconButton
            label="打开项目库"
            onClick={() =>
              void listProjects()
                .then(setProjects)
                .catch((e) => report(store, e))
            }
          >
            <FolderOpen size={16} />
          </IconButton>
          <button className="header-button save-button" onClick={() => void save()}>
            <Save size={14} />
            <span>保存</span>
          </button>
          <button
            className="header-button"
            onClick={() => void exportSceneEffect()}
            title="导出 Vulkan 实时渲染所需的贴图、几何、曲线和着色器"
          >
            导出 Vulkan 特效
          </button>
          <button className="primary-button export-button" onClick={() => void exportEffect()}>
            <Download size={14} /> 导出效果
          </button>
        </div>
      </header>
      <div className="workspace">
        <Library
          store={store}
          onPreset={(id) => void preset(id)}
          onImport={() => assetFile.current?.click()}
          onShader={openShader}
        />
        <main className="viewport-panel">
          <div className="viewport-toolbar">
            <div className="viewport-tools">
              <IconButton
                label="环绕视角 · Esc"
                active={tool === 'orbit'}
                onClick={() => changeTool('orbit')}
              >
                <Hand size={15} />
              </IconButton>
              <IconButton
                label="移动 · W"
                active={tool === 'translate'}
                onClick={() => changeTool('translate')}
              >
                <Move size={15} />
              </IconButton>
              <IconButton
                label="旋转 · E"
                active={tool === 'rotate'}
                onClick={() => changeTool('rotate')}
              >
                <Rotate3D size={15} />
              </IconButton>
              <IconButton
                label="缩放 · R"
                active={tool === 'scale'}
                onClick={() => changeTool('scale')}
              >
                <Scaling size={15} />
              </IconButton>
              <span className="toolbar-separator" />
              <select
                aria-label="预览场景"
                value={stage}
                onChange={(e) => {
                  const mode = e.target.value as typeof stage;
                  setStage(mode);
                  engine?.setStage(mode);
                }}
              >
                <option value="dark">暗色摄影棚</option>
                <option value="light">亮色摄影棚</option>
                <option value="arena">场景遮挡测试</option>
              </select>
            </div>
            <div className="viewport-tools">
              <IconButton
                label="地面网格"
                active={grid}
                onClick={() => {
                  setGrid(!grid);
                  if (engine) engine.stage.grid.visible = !grid;
                }}
              >
                <Grid2X2 size={14} />
              </IconButton>
              <IconButton
                label="1.8 米参照"
                active={reference}
                onClick={() => {
                  setReference(!reference);
                  if (engine) engine.stage.reference.visible = !reference;
                }}
              >
                <UserRound size={14} />
              </IconButton>
              <IconButton label="重置视角 · F" onClick={() => engine?.resetCamera()}>
                <Focus size={14} />
              </IconButton>
              <IconButton label="Shader 工作台" onClick={() => openShader()}>
                <Braces size={14} />
              </IconButton>
              <IconButton
                label="切换检查器"
                active={showInspector}
                onClick={() => setShowInspector(!showInspector)}
              >
                <ChevronRight size={14} />
              </IconButton>
            </div>
          </div>
          <div className="canvas-wrap">
            <canvas ref={canvas} aria-label="Three.js 特效视口" tabIndex={0} />
            <ViewportLabels store={store} lastGoodRevision={engine?.lastGoodRevision ?? 0} />
            <div className="viewport-bottom">
              <div className="performance-chip">
                <span className="performance-led" />
                <b>{metrics ? Math.round(metrics.fps) : '—'}</b> FPS <i />
                <span>{metrics?.drawCalls ?? '—'} DRAW</span>
                <i />
                <span>{metrics?.activeParticles ?? '—'} 粒子</span>
              </div>
              <div className="viewport-hint">
                拖动旋转 · 滚轮缩放 <kbd>F</kbd> 重置
              </div>
            </div>
            <div className="axis-marker">
              <span>Y</span>
              <i />
              <b>X</b>
              <small>Z</small>
            </div>
            {diagnostics.length > 0 && (
              <button className="viewport-error" onClick={() => setBottomTab('diagnostics')}>
                <TriangleAlert size={13} /> {diagnostics.length} 项诊断 · 点击查看
              </button>
            )}
          </div>
        </main>
        <Timeline
          store={store}
          tab={bottomTab}
          setTab={setBottomTab}
          curveTarget={curveTarget}
          setCurveTarget={setCurveTarget}
        />
        <Inspector store={store} onCurve={openCurve} onShader={() => openShader()} />
      </div>
      <footer className="app-status">
        <span className="status-message">
          <i className={diagnostics.length ? 'warning' : ''} />
          {state.status}
        </span>
        <span>
          WebGL2 <i /> Three.js 0.180.0
        </span>
        <span>r{revision}</span>
        <button onClick={() => setHelpOpen(true)}>
          <HelpCircle size={12} />
          <span>快捷键</span>
        </button>
      </footer>
      <input
        ref={openFile}
        type="file"
        hidden
        accept=".json,.zip"
        onChange={(e) => {
          void files(e.target.files);
          e.target.value = '';
        }}
      />
      <input
        ref={assetFile}
        type="file"
        hidden
        multiple
        accept="image/png,image/jpeg,image/webp,.glb"
        onChange={(e) => {
          void files(e.target.files);
          e.target.value = '';
        }}
      />
      {dropActive && (
        <div className="drop-overlay">
          <UploadIcon />
          <h2>将素材放入工作区</h2>
          <p>PNG · JPEG · WebP · GLB · GLSL · 项目 JSON / ZIP</p>
        </div>
      )}
      {shaderOpen && engine && (
        <ShaderWorkbench
          key={shaderId ?? 'new-' + store.state.selectedId}
          store={store}
          engine={engine}
          initialId={shaderId}
          onClose={() => setShaderOpen(false)}
        />
      )}
      {exporting && (
        <Modal title="导出游戏特效" kicker="VALIDATE / RENDER / PACKAGE" onClose={cancelExport}>
          <div className="export-progress">
            <Loader2 className="spin" size={32} />
            <h3>{exporting}</h3>
          </div>
        </Modal>
      )}
      {projects && (
        <Modal title="本机项目库" kicker="LOCAL PROJECTS" onClose={() => setProjects(null)}>
          <div className="project-list">
            <button className="import-project" onClick={() => openFile.current?.click()}>
              <FolderOpen size={18} />
              <div>
                <strong>从文件打开</strong>
                <small>.ignis.json / .ignis.zip</small>
              </div>
              <ArrowUpRight size={16} />
            </button>
            {projects.map((p) => (
              <button
                key={p.id}
                data-project-id={p.id}
                onClick={() => {
                  void saveProject(store.state.doc)
                    .then(() => loadProject(p.id))
                    .then((d) => {
                      store.replace(d, '打开本机项目');
                      setProjects(null);
                    })
                    .catch((e) => report(store, e));
                }}
              >
                <span className="project-icon">✧</span>
                <div>
                  <strong>{p.name}</strong>
                  <small>
                    {new Date(p.updatedAt).toLocaleString()} · {p.doc.layers.length} 图层
                  </small>
                </div>
                <ChevronRight size={14} />
              </button>
            ))}
            {!projects.length && <p>项目会自动保存在当前浏览器中。</p>}
          </div>
        </Modal>
      )}
      {helpOpen && (
        <Modal title="创作快捷键" kicker="VulkCore" onClose={() => setHelpOpen(false)}>
          <div className="shortcut-list">
            {[
              ['Space', '播放 / 暂停'],
              ['W / E / R', '移动 / 旋转 / 缩放'],
              ['Esc', '环绕模式 / 关闭窗口'],
              ['F', '重置视角'],
              ['⌘ / Ctrl + S', '保存并下载项目'],
              ['⌘ / Ctrl + Z', '撤销'],
              ['⌘ / Ctrl + Shift + Z', '重做'],
              ['⌘ / Ctrl + D', '复制图层'],
              ['Delete', '删除所选图层'],
            ].map(([k, v]) => (
              <div key={k}>
                <span>{v}</span>
                <kbd>{k}</kbd>
              </div>
            ))}
            <p>
              时间轴：拖动光标定位；拖动图层移动；拖动右端裁切。曲线：双击添加，拖动调整，右键删除。
            </p>
          </div>
        </Modal>
      )}
    </div>
  );
}
function ViewportLabels({
  store,
  lastGoodRevision,
}: {
  store: EditorStore;
  lastGoodRevision: number;
}) {
  const time = usePlayhead(store);
  return (
    <>
      <div className="viewport-heading">
        <div>
          <span className="live-badge">
            <i />
            {store.playback.playing ? 'LIVE PREVIEW' : 'PAUSED'}
          </span>
          <span className="viewport-engine">透视视图</span>
        </div>
        <small>
          {store.state.doc.layers.length} LAYERS <span>·</span>{' '}
          {store.state.doc.duration.toFixed(1)}s <span>·</span> SEED {store.state.doc.seed}
        </small>
      </div>
      <div className="viewport-time">
        {time.toFixed(2)}
        <span> s</span>
        {lastGoodRevision !== store.state.revision && (
          <small>
            画面 r{lastGoodRevision} / 编辑 r{store.state.revision}
          </small>
        )}
      </div>
    </>
  );
}
function UploadIcon() {
  return (
    <div className="drop-icon">
      <Download size={36} />
    </div>
  );
}
