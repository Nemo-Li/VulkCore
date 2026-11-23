import { useEffect, useRef, useState } from 'react';
import { Plus, Trash2, Diamond } from 'lucide-react';
import { EditorStore } from '../core/store';
import { evaluateCurve, getPath, type Curve } from '../core/model';
import { useEditor, apply, IconButton } from './controls';
export function CurveEditor({
  store,
  target,
  setTarget,
  time,
}: {
  store: EditorStore;
  target: string;
  setTarget: (s: string) => void;
  time: number;
}) {
  const { doc, selectedId } = useEditor(store),
    layer = doc.layers.find((l) => l.id === selectedId);
  const canvas = useRef<HTMLCanvasElement>(null);
  const [size, setSize] = useState({ w: 600, h: 160 });
  const [version, setVersion] = useState(0);
  const drag = useRef<{ index: number; min: number; max: number } | null>(null);
  const targets = [
    ...(layer?.kind !== 'light' && (layer?.kind !== 'postprocess' || layer.post.mode === 'custom')
      ? [
          { value: 'material.opacity', label: '材质透明度' },
          { value: 'material.intensity', label: '发光强度' },
        ]
      : []),
    ...['0', '1', '2'].map((n) => ({
      value: 'transform.position.' + n,
      label: '位置 ' + 'XYZ'[Number(n)],
    })),
    ...['0', '1', '2'].map((n) => ({
      value: 'transform.scale.' + n,
      label: '缩放 ' + 'XYZ'[Number(n)],
    })),
  ];
  if (layer?.kind === 'particles')
    targets.push(
      { value: 'life.size', label: '粒子尺寸 / 寿命' },
      { value: 'life.opacity', label: '透明度 / 寿命' },
      { value: 'emitter.speed', label: '发射速度' },
      { value: 'emitter.size', label: '粒子尺寸' },
    );
  if (layer?.kind === 'mesh') {
    if (layer.mesh.shape === 'plane') targets.push({ value: 'mesh.width', label: '平面宽度' });
    else
      targets.push({
        value: 'mesh.radius',
        label: layer.mesh.shape === 'model' ? '模型倍率' : '网格半径',
      });
    if (['plane', 'cone', 'cylinder'].includes(layer.mesh.shape))
      targets.push({ value: 'mesh.height', label: '几何高度' });
    targets.push({ value: 'transform.rotation.1', label: '旋转 Y（rad）' });
  }
  if (layer?.kind === 'ribbon')
    targets.push(
      { value: 'ribbon.width', label: '拖尾宽度' },
      { value: 'ribbon.radius', label: '路径半径' },
    );
  if (layer?.kind === 'light') targets.push({ value: 'light.intensity', label: '灯光强度' });
  if (layer?.kind === 'postprocess') {
    targets.push({ value: 'post.strength', label: '后处理强度' });
    if (['distortion', 'custom'].includes(layer.post.mode))
      targets.push({ value: 'post.radius', label: '遮罩半径' });
  }
  const shader = doc.shaders.find(
    (s) =>
      s.id ===
      (layer?.kind === 'postprocess'
        ? layer.post.mode === 'custom'
          ? layer.post.shaderId
          : ''
        : layer?.material.shaderId),
  );
  for (const u of shader?.uniforms ?? [])
    if (u.type === 'float') targets.push({ value: 'uniforms.' + u.name, label: u.label ?? u.name });
  const cv = layer?.curves[target],
    isLife = target.startsWith('life.'),
    maxTime = isLife ? 1 : (layer?.duration ?? 1),
    base = isLife
      ? 1
      : target.startsWith('uniforms.')
        ? Number(
            layer?.material.uniforms[target.slice(9)] ??
              shader?.uniforms.find((u) => u.name === target.slice(9))?.default ??
              1,
          )
        : Number(getPath(layer, target) ?? 1);
  const keys = cv?.keys ?? [
      { time: 0, value: base },
      { time: maxTime, value: base },
    ],
    lo = Math.min(0, ...keys.map((k) => k.value)),
    hi = Math.max(1, ...keys.map((k) => k.value)),
    range = drag.current ?? { min: lo - (hi - lo) * 0.1, max: hi + (hi - lo) * 0.15 };
  const left = 35,
    right = 15,
    top = 15,
    bottom = 23;
  useEffect(() => {
    if (layer && !targets.some((t) => t.value === target)) setTarget(targets[0].value);
  }, [selectedId, target, layer?.mesh.shape, layer?.post.mode]);
  useEffect(() => {
    const el = canvas.current?.parentElement;
    if (!el) return;
    const ob = new ResizeObserver(() => {
      const r = el.getBoundingClientRect();
      setSize({ w: r.width, h: r.height });
    });
    ob.observe(el);
    return () => ob.disconnect();
  }, []);
  const map = (t: number, v: number) => [
    left + (t / maxTime) * (size.w - left - right),
    top + ((range.max - v) / (range.max - range.min)) * (size.h - top - bottom),
  ];
  function position(e: { clientX: number; clientY: number }) {
    const rect = canvas.current!.getBoundingClientRect();
    return {
      time: Math.max(
        0,
        Math.min(maxTime, ((e.clientX - rect.left - left) / (size.w - left - right)) * maxTime),
      ),
      value:
        range.max -
        ((e.clientY - rect.top - top) / (size.h - top - bottom)) * (range.max - range.min),
    };
  }
  const update = (next: Curve | null) => {
    if (layer)
      apply(
        store,
        [{ type: 'setCurve', targetId: layer.id, parameter: target, curve: next }],
        `编辑 ${target} 曲线`,
        'curve-' + layer.id + target,
      );
  };
  useEffect(() => {
    const el = canvas.current;
    if (!el || !layer) return;
    const dpr = Math.min(devicePixelRatio, 2);
    el.width = size.w * dpr;
    el.height = size.h * dpr;
    const ctx = el.getContext('2d')!;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, size.w, size.h);
    ctx.fillStyle = '#15191f';
    ctx.fillRect(0, 0, size.w, size.h);
    ctx.font = '10px ui-monospace,monospace';
    ctx.lineWidth = 1;
    for (let i = 0; i <= 4; i++) {
      const x = left + ((size.w - left - right) * i) / 4,
        y = top + ((size.h - top - bottom) * i) / 4;
      ctx.strokeStyle = '#262d36';
      ctx.beginPath();
      ctx.moveTo(x, top);
      ctx.lineTo(x, size.h - bottom);
      ctx.moveTo(left, y);
      ctx.lineTo(size.w - right, y);
      ctx.stroke();
      ctx.fillStyle = '#77818f';
      ctx.textAlign = 'center';
      ctx.fillText(((maxTime * i) / 4).toFixed(isLife ? 2 : 1), x, size.h - 6);
      ctx.textAlign = 'right';
      ctx.fillText((range.max - ((range.max - range.min) * i) / 4).toFixed(1), left - 7, y + 3);
    }
    const curve: Curve = cv ?? { domain: isLife ? 'life' : 'layer', interpolation: 'linear', keys };
    ctx.strokeStyle = '#e8af73';
    ctx.lineWidth = 1.8;
    ctx.beginPath();
    for (let i = 0; i <= 200; i++) {
      const t = (maxTime * i) / 200,
        [x, y] = map(t, evaluateCurve(curve, t, base));
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.stroke();
    if (cv)
      for (const k of keys) {
        const [x, y] = map(k.time, k.value);
        ctx.fillStyle = '#15191f';
        ctx.strokeStyle = '#f0bb81';
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.moveTo(x, y - 4);
        ctx.lineTo(x + 4, y);
        ctx.lineTo(x, y + 4);
        ctx.lineTo(x - 4, y);
        ctx.closePath();
        ctx.fill();
        ctx.stroke();
      }
    const local = isLife
      ? (time - layer.start) / Math.max(0.02, layer.emitter.lifetime)
      : time - layer.start;
    if (local >= 0 && local <= maxTime) {
      const [x] = map(local, 0);
      ctx.strokeStyle = '#a5c2d1';
      ctx.lineWidth = 1;
      ctx.setLineDash([3, 3]);
      ctx.beginPath();
      ctx.moveTo(x, top);
      ctx.lineTo(x, size.h - bottom);
      ctx.stroke();
      ctx.setLineDash([]);
    }
  }, [cv, selectedId, target, size, time, version]);
  if (!layer)
    return (
      <div className="empty-state">
        <Diamond size={20} />
        <p>选择图层以编辑曲线</p>
      </div>
    );
  const add = () => {
    if (!cv)
      update({
        domain: isLife ? 'life' : 'layer',
        interpolation: 'smooth',
        keys: [
          { time: 0, value: base },
          { time: maxTime, value: base },
        ],
      });
  };
  return (
    <div className="curve-panel">
      <div className="curve-toolbar">
        <select aria-label="曲线参数" value={target} onChange={(e) => setTarget(e.target.value)}>
          {targets.map((t) => (
            <option key={t.value} value={t.value}>
              {t.label}
            </option>
          ))}
        </select>
        <select
          aria-label="曲线插值"
          disabled={!cv}
          value={cv?.interpolation ?? 'smooth'}
          onChange={(e) =>
            cv && update({ ...cv, interpolation: e.target.value as Curve['interpolation'] })
          }
        >
          <option value="smooth">平滑</option>
          <option value="linear">线性</option>
          <option value="step">阶梯</option>
        </select>
        {!cv ? (
          <button className="secondary-button" onClick={add}>
            <Plus size={12} /> 添加曲线
          </button>
        ) : (
          <IconButton label="移除曲线" onClick={() => update(null)}>
            <Trash2 size={12} />
          </IconButton>
        )}
        <span>{isLife ? '归一化寿命 0–1' : '图层本地时间 / 秒'} · 双击添加 · 右键删除</span>
      </div>
      <div className="curve-content">
        <div className="curve-canvas-wrap">
          <canvas
            ref={canvas}
            aria-label="参数曲线画布"
            onDoubleClick={(e) => {
              const p = position(e),
                next: Curve = structuredClone(
                  cv ?? {
                    domain: isLife ? 'life' : 'layer',
                    interpolation: 'smooth',
                    keys: [
                      { time: 0, value: base },
                      { time: maxTime, value: base },
                    ],
                  },
                );
              const existing = next.keys.find((k) => Math.abs(k.time - p.time) < 0.005);
              if (existing) existing.value = p.value;
              else
                next.keys.push({
                  time: Number(p.time.toFixed(3)),
                  value: Number(p.value.toFixed(3)),
                });
              next.keys.sort((a, b) => a.time - b.time);
              update(next);
            }}
            onPointerDown={(e) => {
              if (!cv) return;
              const rect = e.currentTarget.getBoundingClientRect();
              const i = cv.keys.findIndex((k) => {
                const [x, y] = map(k.time, k.value);
                return Math.hypot(e.clientX - rect.left - x, e.clientY - rect.top - y) < 11;
              });
              if (i >= 0) {
                drag.current = { index: i, min: range.min, max: range.max };
                store.playback.playing = false;
                e.currentTarget.setPointerCapture(e.pointerId);
              }
            }}
            onPointerMove={(e) => {
              if (!drag.current || !cv) return;
              const p = position(e),
                next = structuredClone(cv),
                i = drag.current.index;
              const t = Math.max(
                i ? next.keys[i - 1].time + 0.001 : 0,
                Math.min(
                  i < next.keys.length - 1 ? next.keys[i + 1].time - 0.001 : maxTime,
                  p.time,
                ),
              );
              next.keys[i] = { time: Number(t.toFixed(3)), value: Number(p.value.toFixed(3)) };
              update(next);
            }}
            onPointerUp={() => {
              drag.current = null;
              setVersion((v) => v + 1);
            }}
            onContextMenu={(e) => {
              e.preventDefault();
              if (!cv) return;
              const r = e.currentTarget.getBoundingClientRect(),
                i = cv.keys.findIndex((k) => {
                  const [x, y] = map(k.time, k.value);
                  return Math.hypot(e.clientX - r.left - x, e.clientY - r.top - y) < 14;
                });
              if (i < 0) return;
              const next = structuredClone(cv);
              next.keys.splice(i, 1);
              update(next.keys.length ? next : null);
            }}
          />
        </div>
        <div className="curve-key-list">
          <div>
            <span>时间</span>
            <span>数值</span>
          </div>
          {cv?.keys.map((k, i) => (
            <div key={i}>
              <input
                aria-label={`关键帧${i}时间`}
                type="number"
                step="0.01"
                value={Number(k.time.toFixed(3))}
                onChange={(e) => {
                  const t = Number(e.target.value);
                  if (t < 0 || t > maxTime) return;
                  const next = structuredClone(cv);
                  next.keys[i].time = t;
                  next.keys.sort((a, b) => a.time - b.time);
                  update(next);
                }}
              />
              <input
                aria-label={`关键帧${i}数值`}
                type="number"
                step="0.05"
                value={Number(k.value.toFixed(3))}
                onChange={(e) => {
                  const next = structuredClone(cv);
                  next.keys[i].value = Number(e.target.value);
                  update(next);
                }}
              />
            </div>
          ))}
          {!cv && <p>添加曲线后，可在画布中调整关键帧。</p>}
        </div>
      </div>
    </div>
  );
}
