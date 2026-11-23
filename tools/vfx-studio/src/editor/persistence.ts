import { cloneDocument, parseDocument, type EffectDocument } from '../core/model';
import { sha256Hex } from '../core/crypto';
export type SavedProject = { id: string; name: string; updatedAt: number; doc: EffectDocument };
const sourcePrefix = 'ignis-db://';
let database: Promise<IDBDatabase> | undefined;
const sourceKeys = new Map<string, { source: string; key: string }>();
let cachedSourceBytes = 0;
function db() {
  return (database ??= new Promise<IDBDatabase>((resolve, reject) => {
    const request = indexedDB.open('ignis-vfx-studio', 2);
    request.onupgradeneeded = () => {
      const d = request.result;
      if (!d.objectStoreNames.contains('projects'))
        d.createObjectStore('projects', { keyPath: 'id' });
      if (!d.objectStoreNames.contains('assetSources'))
        d.createObjectStore('assetSources', { keyPath: 'key' });
    };
    request.onsuccess = () => {
      request.result.onversionchange = () => {
        request.result.close();
        database = undefined;
      };
      resolve(request.result);
    };
    request.onerror = () => reject(request.error);
  }));
}
function cacheSource(id: string, source: string, key: string) {
  const previous = sourceKeys.get(id);
  if (previous) cachedSourceBytes -= previous.source.length * 2;
  sourceKeys.delete(id);
  sourceKeys.set(id, { source, key });
  cachedSourceBytes += source.length * 2;
  while (sourceKeys.size > 200 || cachedSourceBytes > 512 * 1024 * 1024) {
    const first = sourceKeys.keys().next().value!;
    cachedSourceBytes -= sourceKeys.get(first)!.source.length * 2;
    sourceKeys.delete(first);
  }
}
async function keyForSource(id: string, source: string) {
  const cached = sourceKeys.get(id);
  if (cached?.source === source) return cached.key;
  const key = await sha256Hex(new TextEncoder().encode(source));
  cacheSource(id, source, key);
  return key;
}
export async function saveProject(input: EffectDocument) {
  const doc = cloneDocument(input),
    sources: { key: string; source: string }[] = [];
  await Promise.all(
    doc.assets.map(async (asset) => {
      if (!asset.src.startsWith('data:')) return;
      const source = asset.src,
        key = await keyForSource(asset.id, source);
      sources.push({ key, source });
      asset.src = sourcePrefix + key;
    }),
  );
  const d = await db();
  await new Promise<void>((resolve, reject) => {
    const transaction = d.transaction(['projects', 'assetSources'], 'readwrite'),
      store = transaction.objectStore('assetSources');
    for (const source of sources) {
      const existing = store.getKey(source.key);
      existing.onsuccess = () => {
        if (existing.result === undefined) store.put(source);
      };
    }
    transaction
      .objectStore('projects')
      .put({ id: doc.id, name: doc.name, updatedAt: Date.now(), doc });
    transaction.oncomplete = () => resolve();
    transaction.onerror = () => reject(transaction.error);
    transaction.onabort = () => reject(transaction.error ?? new Error('项目保存已中止'));
  });
}
export async function listProjects(): Promise<SavedProject[]> {
  const d = await db();
  return new Promise((resolve, reject) => {
    const request = d.transaction('projects').objectStore('projects').getAll();
    request.onsuccess = () =>
      resolve((request.result as SavedProject[]).sort((a, b) => b.updatedAt - a.updatedAt));
    request.onerror = () => reject(request.error);
  });
}
export async function loadProject(id: string): Promise<EffectDocument> {
  const d = await db(),
    record = await new Promise<SavedProject | undefined>((resolve, reject) => {
      const request = d.transaction('projects').objectStore('projects').get(id);
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error);
    });
  if (!record) throw new Error('本机项目不存在');
  const doc = cloneDocument(record.doc);
  await Promise.all(
    doc.assets.map(async (asset) => {
      if (!asset.src.startsWith(sourcePrefix)) return;
      const key = asset.src.slice(sourcePrefix.length),
        source = await new Promise<{ source: string } | undefined>((resolve, reject) => {
          const request = d.transaction('assetSources').objectStore('assetSources').get(key);
          request.onsuccess = () => resolve(request.result);
          request.onerror = () => reject(request.error);
        });
      if (!source) throw new Error('本机素材缺失：' + asset.name);
      asset.src = source.source;
      cacheSource(asset.id, asset.src, key);
    }),
  );
  return parseDocument(doc);
}
export async function loadRecent(): Promise<EffectDocument | null> {
  try {
    const p = (await listProjects())[0];
    return p ? await loadProject(p.id) : null;
  } catch {
    return null;
  }
}
export async function deleteProject(id: string) {
  const d = await db();
  await new Promise<void>((resolve, reject) => {
    const tx = d.transaction('projects', 'readwrite');
    tx.objectStore('projects').delete(id);
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
  const referenced = new Set(
    (await listProjects()).flatMap((p) =>
      p.doc.assets
        .filter((a) => a.src.startsWith(sourcePrefix))
        .map((a) => a.src.slice(sourcePrefix.length)),
    ),
  );
  await new Promise<void>((resolve, reject) => {
    const tx = d.transaction('assetSources', 'readwrite'),
      store = tx.objectStore('assetSources'),
      cursor = store.openKeyCursor();
    cursor.onsuccess = () => {
      const c = cursor.result;
      if (c) {
        if (!referenced.has(String(c.key))) store.delete(c.key);
        c.continue();
      }
    };
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
}
export function downloadBlob(blob: Blob, name: string) {
  const url = URL.createObjectURL(blob),
    a = document.createElement('a');
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 20000);
}
export function downloadProject(doc: EffectDocument) {
  downloadBlob(
    new Blob([JSON.stringify(doc, null, 2)], { type: 'application/json' }),
    doc.name + '.ignis.json',
  );
}
