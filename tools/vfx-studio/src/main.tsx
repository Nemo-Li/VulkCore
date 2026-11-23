import { createRoot } from 'react-dom/client';
import App from './App';
import { EditorStore } from './core/store';
import { createPreset } from './core/presets';
import { loadRecent } from './editor/persistence';
const initial = await loadRecent().catch(() => null);
const store = new EditorStore(initial ?? createPreset('arcane'));
createRoot(document.getElementById('root')!).render(<App store={store} />);
