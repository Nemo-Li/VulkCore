import type { EditorStore } from './core/store';
import type { EditorEngine } from './editor/engine';
declare global {
  interface Window {
    __IGNIS__?: {
      store: EditorStore;
      engine: EditorEngine;
      ready: boolean;
      setTime: (t: number) => void;
      loadPreset: (id: string) => Promise<void>;
      getDocument: () => unknown;
      getMetrics: () => unknown;
    };
  }
}
export {};
