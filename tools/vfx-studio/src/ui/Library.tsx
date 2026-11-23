import { useState } from 'react';
import {
  Plus,
  Search,
  Layers,
  Eye,
  EyeOff,
  Copy,
  Trash2,
  Upload,
  Box,
  Sparkles,
  Waves,
  Sun,
  ScanLine,
  Braces,
  Headphones,
} from 'lucide-react';
import { EditorStore } from '../core/store';
import { KIND_LABEL, makeLayer, type LayerKind } from '../core/model';
import { PRESETS } from '../core/presets';
import { useEditor, apply, IconButton } from './controls';
const KindIcon = {
  particles: Sparkles,
  mesh: Box,
  ribbon: Waves,
  light: Sun,
  postprocess: ScanLine,
};
export function Library({
  store,
  onPreset,
  onImport,
  onShader,
}: {
  store: EditorStore;
  onPreset: (id: string) => void;
  onImport: () => void;
  onShader: (id?: string) => void;
}) {
  const { doc, selectedId } = useEditor(store);
  const [tab, setTab] = useState('layers'),
    [search, setSearch] = useState(''),
    [menu, setMenu] = useState(false),
    [filter, setFilter] = useState('all');
  const add = (kind: LayerKind) => {
    const layer = makeLayer(kind);
    layer.duration = Math.min(layer.duration, doc.duration);
    if (apply(store, [{ type: 'addLayer', kind, layer }], '添加' + KIND_LABEL[kind]))
      store.select(layer.id);
    setMenu(false);
  };
  return (
    <aside className="library-panel">
      <div className="library-title">
        <Layers size={15} />
        <strong>图层与资源</strong>
      </div>
      <div className="panel-tabs">
        <button className={tab === 'layers' ? 'selected' : ''} onClick={() => setTab('layers')}>
          图层 <small>{doc.layers.length}</small>
        </button>
        <button className={tab === 'assets' ? 'selected' : ''} onClick={() => setTab('assets')}>
          资源库
        </button>
      </div>
      <div className="search-field">
        <Search size={13} />
        <input
          aria-label="搜索图层或资源"
          placeholder={tab === 'layers' ? '搜索图层…' : '搜索素材与预设…'}
          value={search}
          onChange={(e) => setSearch(e.target.value)}
        />
        <kbd>⌕</kbd>
      </div>
      {tab === 'layers' ? (
        <>
          <div className="layer-list-heading">
            <span>效果图层</span>
            <div className="add-layer-wrap">
              <IconButton label="添加图层" onClick={() => setMenu(!menu)}>
                <Plus size={15} />
              </IconButton>
              {menu && (
                <div className="add-menu">
                  {Object.entries(KIND_LABEL).map(([kind, label]) => {
                    const Icon = KindIcon[kind as LayerKind];
                    return (
                      <button key={kind} onClick={() => add(kind as LayerKind)}>
                        <Icon size={14} />
                        {label}
                      </button>
                    );
                  })}
                </div>
              )}
            </div>
          </div>
          <div className="layer-list">
            {doc.layers
              .filter((l) => l.name.toLowerCase().includes(search.toLowerCase()))
              .map((l, i) => {
                const Icon = KindIcon[l.kind];
                return (
                  <div
                    key={l.id}
                    className={`layer-row ${l.id === selectedId ? 'selected' : ''} ${!l.enabled ? 'muted' : ''}`}
                    data-layer-id={l.id}
                    onClick={() => store.select(l.id)}
                    draggable
                    onDragStart={(e) => e.dataTransfer.setData('application/ignis-layer', l.id)}
                    onDragOver={(e) => {
                      if (e.dataTransfer.types.includes('application/ignis-layer'))
                        e.preventDefault();
                    }}
                    onDrop={(e) => {
                      const id = e.dataTransfer.getData('application/ignis-layer');
                      if (id) {
                        e.preventDefault();
                        apply(
                          store,
                          [{ type: 'reorderLayer', targetId: id, index: i }],
                          '排序图层',
                        );
                      }
                    }}
                  >
                    <span className="layer-icon" style={{ color: l.material.color }}>
                      <Icon size={15} />
                    </span>
                    <div className="layer-name">
                      <span>{l.name}</span>
                      <small>{KIND_LABEL[l.kind]}</small>
                    </div>
                    <button
                      className={`layer-solo ${store.soloId === l.id ? 'on' : ''}`}
                      title="独奏图层"
                      aria-label={`独奏 ${l.name}`}
                      onClick={(e) => {
                        e.stopPropagation();
                        store.soloId = store.soloId === l.id ? null : l.id;
                        store.status(store.soloId ? '已独奏图层' : '显示全部图层');
                      }}
                    >
                      <Headphones size={12} />
                    </button>
                    <button
                      className="layer-eye"
                      title="显示/隐藏图层"
                      aria-label={`切换 ${l.name}`}
                      onClick={(e) => {
                        e.stopPropagation();
                        apply(
                          store,
                          [
                            {
                              type: 'setParameter',
                              targetId: l.id,
                              name: 'enabled',
                              value: !l.enabled,
                            },
                          ],
                          '切换图层可见性',
                        );
                      }}
                    >
                      {l.enabled ? <Eye size={13} /> : <EyeOff size={13} />}
                    </button>
                  </div>
                );
              })}
            {!doc.layers.length && (
              <div className="empty-state">
                <Sparkles size={24} />
                <p>从一个图层开始</p>
                <small>添加粒子、网格或拖尾</small>
              </div>
            )}
          </div>
          <div className="layer-actions">
            <IconButton
              label="复制图层"
              disabled={!selectedId}
              onClick={() =>
                selectedId &&
                apply(store, [{ type: 'duplicateLayer', targetId: selectedId }], '复制图层')
              }
            >
              <Copy size={14} />
            </IconButton>
            <IconButton
              label="删除图层"
              disabled={!selectedId}
              onClick={() =>
                selectedId &&
                apply(store, [{ type: 'removeLayer', targetId: selectedId }], '删除图层')
              }
            >
              <Trash2 size={14} />
            </IconButton>
            <span>{doc.layers.filter((l) => l.enabled).length} 个启用</span>
          </div>
          <div className="preset-teaser">
            <button
              onClick={() => {
                setTab('assets');
                setFilter('preset');
              }}
            >
              浏览特效预设 <span>↗</span>
            </button>
          </div>
        </>
      ) : (
        <>
          <div className="asset-filters">
            {[
              ['all', '全部'],
              ['preset', '预设'],
              ['texture', '贴图'],
              ['shader', 'Shader'],
              ['model', '模型'],
            ].map(([value, label]) => (
              <button
                key={value}
                className={filter === value ? 'active' : ''}
                onClick={() => setFilter(value)}
              >
                {label}
              </button>
            ))}
          </div>
          <div className="asset-scroll">
            {['all', 'preset'].includes(filter) && (
              <>
                <div className="asset-section-label">
                  特效预设 <span>08</span>
                </div>
                <div className="preset-grid">
                  {PRESETS.filter((p) =>
                    (p.name + p.en + p.tag).toLowerCase().includes(search.toLowerCase()),
                  ).map((p, i) => (
                    <button
                      key={p.id}
                      className="preset-card"
                      aria-label={`加载${p.name}`}
                      onClick={() => onPreset(p.id)}
                    >
                      <div
                        className={`preset-art art-${i}`}
                        style={{ '--preset-color': p.color } as React.CSSProperties}
                      >
                        <i />
                        <i />
                        <span>✧</span>
                        <small>{p.tag}</small>
                      </div>
                      <strong>{p.name}</strong>
                      <small>
                        {p.duration.toFixed(1)}s · {p.en.split(' ')[0]}
                      </small>
                    </button>
                  ))}
                </div>
              </>
            )}
            {['all', 'texture', 'model'].includes(filter) && (
              <>
                <div className="asset-section-label">
                  项目资源 <span>{doc.assets.length.toString().padStart(2, '0')}</span>
                </div>
                {doc.assets
                  .filter(
                    (a) =>
                      (filter === 'all' || a.type === filter) &&
                      a.name.toLowerCase().includes(search.toLowerCase()),
                  )
                  .map((a) => (
                    <div className="asset-item" key={a.id}>
                      {a.type === 'texture' ? <img src={a.src} alt={a.name} /> : <Box size={22} />}
                      <div>
                        <strong>{a.name}</strong>
                        <small>
                          {(a.bytes / 1024).toFixed(1)} KiB · {a.colorSpace}
                        </small>
                      </div>
                      <IconButton
                        label={`删除资源 ${a.name}`}
                        onClick={() =>
                          apply(store, [{ type: 'removeAsset', targetId: a.id }], '删除资源')
                        }
                      >
                        <Trash2 size={12} />
                      </IconButton>
                    </div>
                  ))}
              </>
            )}
            {['all', 'shader'].includes(filter) && (
              <>
                <div className="asset-section-label">
                  自定义 Shader <span>{doc.shaders.length.toString().padStart(2, '0')}</span>
                </div>
                {doc.shaders.map((s) => (
                  <div className="asset-item shader-item" key={s.id}>
                    <button className="asset-shader-open" onClick={() => onShader(s.id)}>
                      <Braces size={22} />
                      <div>
                        <strong>{s.name}</strong>
                        <small>{s.profile} · GLSL 3</small>
                      </div>
                    </button>
                    <IconButton
                      label={`删除 Shader ${s.name}`}
                      onClick={() =>
                        apply(store, [{ type: 'removeShader', targetId: s.id }], '删除 Shader')
                      }
                    >
                      <Trash2 size={12} />
                    </IconButton>
                  </div>
                ))}
                <button className="secondary-button full-width" onClick={() => onShader()}>
                  <Plus size={13} /> 新建 Shader
                </button>
              </>
            )}
          </div>
          <button className="import-button" onClick={onImport}>
            <Upload size={14} /> 导入贴图 / GLB
          </button>
        </>
      )}
    </aside>
  );
}
