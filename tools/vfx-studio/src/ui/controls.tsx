import { useEffect, useState, useSyncExternalStore, type ReactNode } from 'react';
import { ChevronDown, Diamond } from 'lucide-react';
import { ZodError } from 'zod';
import { EditorStore, type Command } from '../core/store';
export const useEditor = (store: EditorStore) =>
  useSyncExternalStore(store.subscribe, store.getSnapshot);
export function report(store: EditorStore, e: unknown) {
  store.status(
    e instanceof ZodError
      ? `${e.issues[0]?.path.join('.')}：${e.issues[0]?.message}`
      : e instanceof Error
        ? e.message
        : String(e),
  );
}
export function apply(store: EditorStore, commands: Command[], label?: string, group?: string) {
  try {
    return store.apply(commands, { label, group });
  } catch (e) {
    report(store, e);
    return null;
  }
}
export function IconButton({
  children,
  label,
  onClick,
  active = false,
  disabled = false,
  className = '',
}: {
  children: ReactNode;
  label: string;
  onClick?: () => void;
  active?: boolean;
  disabled?: boolean;
  className?: string;
}) {
  return (
    <button
      className={`icon-button ${active ? 'active' : ''} ${className}`}
      aria-label={label}
      title={label}
      disabled={disabled}
      onClick={onClick}
    >
      {children}
    </button>
  );
}
export function Section({
  title,
  children,
  initialOpen = true,
  badge,
}: {
  title: string;
  children: ReactNode;
  initialOpen?: boolean;
  badge?: ReactNode;
}) {
  const [open, setOpen] = useState(initialOpen);
  return (
    <section className="inspector-section">
      <button className="section-title" onClick={() => setOpen(!open)}>
        <ChevronDown size={13} className={open ? '' : 'closed'} />
        <span>{title}</span>
        {badge && <small>{badge}</small>}
      </button>
      {open && <div className="section-content">{children}</div>}
    </section>
  );
}
export function NumberField({
  label,
  value,
  onChange,
  min = -10000,
  max = 10000,
  step = 0.01,
  unit = '',
  slider = false,
  curve = false,
  onCurve,
  disabled = false,
}: {
  label: string;
  value: number;
  onChange: (value: number) => void;
  min?: number;
  max?: number;
  step?: number;
  unit?: string;
  slider?: boolean;
  curve?: boolean;
  onCurve?: () => void;
  disabled?: boolean;
}) {
  const [draft, setDraft] = useState(String(Number(value.toFixed(3))));
  useEffect(() => setDraft(String(Number(value.toFixed(3)))), [value]);
  const commit = () => {
    if (draft === String(Number(value.toFixed(3)))) return;
    const n = Number(draft);
    if (draft.trim() && Number.isFinite(n)) onChange(Math.max(min, Math.min(max, n)));
    else setDraft(String(value));
  };
  return (
    <div className={`field ${slider ? 'with-slider' : ''}`}>
      <div className="field-line">
        <label>{label}</label>
        <div className="field-input">
          <input
            aria-label={label}
            type="number"
            value={draft}
            min={min}
            max={max}
            step={step}
            disabled={disabled}
            onChange={(e) => setDraft(e.target.value)}
            onBlur={commit}
            onKeyDown={(e) => {
              if (e.key === 'Enter') e.currentTarget.blur();
            }}
          />
          {unit && <span>{unit}</span>}
        </div>
        {onCurve && (
          <button
            className={`keyframe-button ${curve ? 'keyed' : ''}`}
            title="编辑参数曲线"
            aria-label={`${label}曲线`}
            onClick={onCurve}
          >
            <Diamond size={10} fill={curve ? 'currentColor' : 'none'} />
          </button>
        )}
      </div>
      {slider && (
        <input
          aria-label={`${label}滑块`}
          className="range-input"
          type="range"
          min={min}
          max={max}
          step={step}
          value={value}
          onChange={(e) => onChange(Number(e.target.value))}
        />
      )}
    </div>
  );
}
export function SelectField({
  label,
  value,
  onChange,
  options,
}: {
  label: string;
  value: string;
  onChange: (s: string) => void;
  options: readonly (string | { value: string; label: string; disabled?: boolean })[];
}) {
  return (
    <div className="field field-line">
      <label>{label}</label>
      <select aria-label={label} value={value} onChange={(e) => onChange(e.target.value)}>
        {options.map((o) =>
          typeof o === 'string' ? (
            <option key={o} value={o}>
              {o}
            </option>
          ) : (
            <option key={o.value} value={o.value} disabled={o.disabled}>
              {o.label}
            </option>
          ),
        )}
      </select>
    </div>
  );
}
export function ColorField({
  label,
  value,
  onChange,
}: {
  label: string;
  value: string;
  onChange: (s: string) => void;
}) {
  return (
    <div className="field field-line">
      <label>{label}</label>
      <div className="color-input">
        <input
          aria-label={label}
          type="color"
          value={value}
          onChange={(e) => onChange(e.target.value)}
        />
        <span>{value.toUpperCase()}</span>
      </div>
    </div>
  );
}
export function ToggleField({
  label,
  value,
  onChange,
}: {
  label: string;
  value: boolean;
  onChange: (v: boolean) => void;
}) {
  return (
    <label className="field field-line toggle-field">
      <span>{label}</span>
      <input
        aria-label={label}
        type="checkbox"
        checked={value}
        onChange={(e) => onChange(e.target.checked)}
      />
      <i />
    </label>
  );
}
export function VectorField({
  label,
  values,
  onChange,
  rotation = false,
}: {
  label: string;
  values: number[];
  onChange: (v: [number, number, number]) => void;
  rotation?: boolean;
}) {
  return (
    <div className="vector-field">
      <label>{label}</label>
      <div>
        {values.map((v, i) => (
          <VectorInput
            key={i}
            axis={i}
            value={rotation ? (v * 180) / Math.PI : v}
            label={`${label}${'XYZ'[i]}`}
            onChange={(n) => {
              const next = [...values] as [number, number, number];
              next[i] = rotation ? (n * Math.PI) / 180 : n;
              onChange(next);
            }}
          />
        ))}
      </div>
    </div>
  );
}
function VectorInput({
  axis,
  value,
  label,
  onChange,
}: {
  axis: number;
  value: number;
  label: string;
  onChange: (n: number) => void;
}) {
  const [v, setV] = useState(String(Number(value.toFixed(2))));
  useEffect(() => setV(String(Number(value.toFixed(2)))), [value]);
  return (
    <span className={`vector-input axis-${axis}`}>
      <small>{'XYZ'[axis]}</small>
      <input
        type="number"
        step="0.1"
        value={v}
        aria-label={label}
        onChange={(e) => setV(e.target.value)}
        onBlur={() => {
          const n = Number(v);
          if (v !== String(Number(value.toFixed(2))) && v && Number.isFinite(n)) onChange(n);
        }}
        onKeyDown={(e) => {
          if (e.key === 'Enter') e.currentTarget.blur();
        }}
      />
    </span>
  );
}
export function TextField({
  label,
  value,
  onChange,
}: {
  label: string;
  value: string;
  onChange: (v: string) => void;
}) {
  const [v, setV] = useState(value);
  useEffect(() => setV(value), [value]);
  return (
    <div className="field">
      <label className="text-field-label">{label}</label>
      <input
        className="text-input"
        aria-label={label}
        value={v}
        onChange={(e) => setV(e.target.value)}
        onBlur={() => {
          if (v.trim() && v !== value) onChange(v.trim());
        }}
        onKeyDown={(e) => {
          if (e.key === 'Enter') e.currentTarget.blur();
        }}
      />
    </div>
  );
}
