import { useEffect, useState } from 'react';
import {
  Play,
  Pause,
  Square,
  Repeat2,
  SkipBack,
  StepBack,
  StepForward,
  Diamond,
  ZoomIn,
  CheckCircle2,
  AlertTriangle,
  Clock3,
} from 'lucide-react';
import { EditorStore } from '../core/store';
import type { Layer } from '../core/model';
import { IconButton, useEditor, apply } from './controls';
import { CurveEditor } from './CurveEditor';
export type BottomTab = 'timeline' | 'curves' | 'diagnostics';
export function usePlayhead(store: EditorStore) {
  const [t, setT] = useState(store.playback.time);
  useEffect(() => {
    const id = setInterval(() => setT(store.playback.time), 50);
    return () => clearInterval(id);
  }, [store]);
  return t;
}
export function Timeline({
  store,
  tab,
  setTab,
  curveTarget,
  setCurveTarget,
}: {
  store: EditorStore;
  tab: BottomTab;
  setTab: (t: BottomTab) => void;
  curveTarget: string;
  setCurveTarget: (s: string) => void;
}) {
  const { doc, selectedId, diagnostics } = useEditor(store),
    time = usePlayhead(store);
  const [zoom, setZoom] = useState(1);
  const seek = (t: number) => {
    store.playback.playing = false;
    store.playback.time = Math.max(0, Math.min(doc.duration, t));
  };
  function dragLayer(e: React.PointerEvent, l: Layer, resize: boolean) {
    e.preventDefault();
    e.stopPropagation();
    store.select(l.id);
    store.playback.playing = false;
    const x = e.clientX,
      width = e.currentTarget.closest('.track-area')!.getBoundingClientRect().width;
    const move = (p: PointerEvent) => {
      const delta = ((p.clientX - x) / width) * doc.duration;
      const value = resize
        ? Math.max(
            0.02,
            Math.min(doc.duration - l.start, Math.round((l.duration + delta) * 100) / 100),
          )
        : Math.max(
            0,
            Math.min(doc.duration - l.duration, Math.round((l.start + delta) * 100) / 100),
          );
      apply(
        store,
        [{ type: 'setParameter', targetId: l.id, name: resize ? 'duration' : 'start', value }],
        resize ? '裁切图层时间' : '移动图层时间',
        'timeline-' + l.id,
      );
    };
    const up = () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  }
  const step = doc.duration <= 2 ? 0.25 : doc.duration <= 5 ? 0.5 : doc.duration <= 12 ? 1 : 5;
  const ticks = Array.from({ length: Math.floor(doc.duration / step) + 1 }, (_, i) => i * step);
  return (
    <section className="bottom-panel">
      <div className="bottom-header">
        <div className="bottom-tabs">
          <button
            className={tab === 'timeline' ? 'selected' : ''}
            onClick={() => setTab('timeline')}
          >
            时间轴
          </button>
          <button className={tab === 'curves' ? 'selected' : ''} onClick={() => setTab('curves')}>
            曲线编辑
          </button>
          <button
            className={tab === 'diagnostics' ? 'selected' : ''}
            onClick={() => setTab('diagnostics')}
          >
            诊断 {diagnostics.length > 0 && <em>{diagnostics.length}</em>}
          </button>
        </div>
        <div className="playback-controls">
          <IconButton label="回到起点" onClick={() => seek(0)}>
            <SkipBack size={13} />
          </IconButton>
          <IconButton label="上一帧" onClick={() => seek(time - 1 / 60)}>
            <StepBack size={13} />
          </IconButton>
          <IconButton
            label={store.playback.playing ? '暂停' : '播放'}
            className="play-button"
            onClick={() => {
              if (time >= doc.duration) store.playback.time = 0;
              store.playback.playing = !store.playback.playing;
            }}
          >
            {store.playback.playing ? (
              <Pause size={14} fill="currentColor" />
            ) : (
              <Play size={14} fill="currentColor" />
            )}
          </IconButton>
          <IconButton label="下一帧" onClick={() => seek(time + 1 / 60)}>
            <StepForward size={13} />
          </IconButton>
          <IconButton label="停止" onClick={() => seek(0)}>
            <Square size={11} />
          </IconButton>
          <IconButton
            label="循环播放"
            active={store.playback.loop}
            onClick={() => {
              const value = !store.playback.loop;
              store.playback.loop = value;
              apply(store, [{ type: 'setDocument', name: 'loop', value }], '切换循环');
            }}
          >
            <Repeat2 size={14} />
          </IconButton>
          <span className="timecode">
            <b>{time.toFixed(2)}</b>
            <span>/ {doc.duration.toFixed(2)} s</span>
          </span>
          <select
            aria-label="播放速度"
            value={store.playback.speed}
            onChange={(e) => (store.playback.speed = Number(e.target.value))}
          >
            {[0.25, 0.5, 1, 2].map((s) => (
              <option key={s} value={s}>
                {s}×
              </option>
            ))}
          </select>
        </div>
      </div>
      {tab === 'timeline' ? (
        <>
          <div className="timeline-scroll">
            <div className="timeline-inner" style={{ width: `${zoom * 100}%` }}>
              <div className="ruler-row">
                <div className="track-label ruler-label">图层 / 通道</div>
                <div
                  className="ruler"
                  onPointerDown={(e) => {
                    const rect = e.currentTarget.getBoundingClientRect();
                    seek(((e.clientX - rect.left) / rect.width) * doc.duration);
                    const move = (p: PointerEvent) =>
                      seek(((p.clientX - rect.left) / rect.width) * doc.duration);
                    const up = () => {
                      window.removeEventListener('pointermove', move);
                      window.removeEventListener('pointerup', up);
                    };
                    window.addEventListener('pointermove', move);
                    window.addEventListener('pointerup', up);
                  }}
                >
                  {ticks.map((t) => (
                    <span key={t} style={{ left: `${(t / doc.duration) * 100}%` }}>
                      {t.toFixed(1)}
                    </span>
                  ))}
                </div>
              </div>
              <div className="timeline-rows">
                {doc.layers.map((l) => (
                  <div
                    key={l.id}
                    className={`timeline-row ${selectedId === l.id ? 'selected' : ''}`}
                  >
                    <button className="track-label" onClick={() => store.select(l.id)}>
                      <i style={{ background: l.material.color }} />
                      <span>{l.name}</span>
                    </button>
                    <div
                      className="track-area"
                      style={{ backgroundSize: `${(step / doc.duration) * 100}% 100%` }}
                      onDoubleClick={(e) => {
                        const r = e.currentTarget.getBoundingClientRect();
                        seek(((e.clientX - r.left) / r.width) * doc.duration);
                      }}
                    >
                      <div
                        className={`track-bar ${!l.enabled ? 'disabled' : ''}`}
                        style={
                          {
                            left: `${(l.start / doc.duration) * 100}%`,
                            width: `${(l.duration / doc.duration) * 100}%`,
                            '--track-color': l.material.color,
                          } as React.CSSProperties
                        }
                        onPointerDown={(e) => dragLayer(e, l, false)}
                        title="拖动移动 · 右端裁切"
                        onDoubleClick={() => {
                          store.select(l.id);
                          setCurveTarget(
                            Object.keys(l.curves).find((k) => !k.startsWith('life.')) ??
                              'material.opacity',
                          );
                          setTab('curves');
                        }}
                      >
                        <b>{l.duration.toFixed(2)} s</b>
                        <span
                          className="clip-handle"
                          onPointerDown={(e) => dragLayer(e, l, true)}
                        />
                      </div>
                      {Object.entries(l.curves)
                        .filter(([, c]) => c.domain === 'layer')
                        .flatMap(([path, c]) =>
                          c.keys.map((k, i) => (
                            <button
                              key={path + i}
                              className="track-key"
                              title={`${path} · ${k.time.toFixed(2)}s`}
                              style={{ left: `${((l.start + k.time) / doc.duration) * 100}%` }}
                              onClick={() => {
                                store.select(l.id);
                                setCurveTarget(path);
                                seek(l.start + k.time);
                                setTab('curves');
                              }}
                            >
                              <Diamond size={7} fill="currentColor" />
                            </button>
                          )),
                        )}
                    </div>
                  </div>
                ))}
                <div
                  className="timeline-playhead"
                  style={{
                    left: `calc(var(--track-label) + (100% - var(--track-label)) * ${time / doc.duration})`,
                  }}
                >
                  <i />
                </div>
              </div>
            </div>
          </div>
          <div className="timeline-footer">
            <span>
              <Clock3 size={11} /> 60 FPS 时间基准
            </span>
            <span>拖动图层移动 · 双击编辑曲线</span>
            <label>
              <ZoomIn size={12} />
              <input
                type="range"
                aria-label="时间轴缩放"
                min={1}
                max={4}
                step={0.25}
                value={zoom}
                onChange={(e) => setZoom(Number(e.target.value))}
              />
            </label>
          </div>
        </>
      ) : tab === 'curves' ? (
        <CurveEditor store={store} target={curveTarget} setTarget={setCurveTarget} time={time} />
      ) : (
        <div className="diagnostic-panel">
          {diagnostics.length ? (
            diagnostics.map((d, i) => (
              <div className={`diagnostic ${d.severity}`} key={i}>
                <AlertTriangle size={15} />
                <div>
                  <strong>{d.code}</strong>
                  <p>
                    {d.file && `${d.file}${d.line ? ':' + d.line : ''} · `}
                    {d.message}
                  </p>
                  <small>
                    {d.targetId} {d.revision ? `revision ${d.revision}` : ''}
                  </small>
                </div>
              </div>
            ))
          ) : (
            <div className="diagnostic-ok">
              <CheckCircle2 size={17} />
              <span>没有待处理诊断</span>
              <small>编译、资源与状态问题会显示在这里</small>
            </div>
          )}
          <div className="history-log">
            {store
              .history()
              .slice(-12)
              .reverse()
              .map((h) => (
                <div key={h.transactionId}>
                  <span>EDIT</span>
                  {h.label}
                  <small>{h.transactionId.slice(-8)}</small>
                </div>
              ))}
          </div>
        </div>
      )}
    </section>
  );
}
