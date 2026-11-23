import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from 'three';
import {
  ANIMATABLE_PATHS,
  cloneDocument,
  curveSchema,
  evaluateCurve,
  makeDocument,
  makeLayer,
  parseDocument,
  setPath,
  type ShaderAsset,
} from '../src/core/model';
import { EditorError, EditorStore } from '../src/core/store';
import { PRESETS, createPreset, curve } from '../src/core/presets';
import { generateParticles, particleState } from '../src/runtime/particles';
import { VfxRuntime } from '../src/runtime/runtime';

const store = () => new EditorStore(createPreset('arcane'));
const shader: ShaderAsset = {
  id: 'test_shader',
  name: 'Test',
  profile: 'mesh',
  vertex: '',
  fragment: '',
  uniforms: [
    { name: 'uGain', type: 'float', default: 1, min: 0, max: 3 },
    { name: 'uOffset', type: 'vec3', default: [0, 0, 0] },
  ],
};
const isCode = (code: string) => (error: unknown) =>
  error instanceof EditorError && error.code === code;

test('all eight presets satisfy document, timing, references and particle budgets', () => {
  assert.equal(PRESETS.length, 8);
  for (const preset of PRESETS) {
    const d = parseDocument(createPreset(preset.id));
    assert.ok(d.layers.length >= 4, preset.id);
    assert.equal(new Set(d.layers.map((l) => l.id)).size, d.layers.length);
    assert.ok(d.layers.every((l) => l.start + l.duration <= d.duration + 1e-6));
  }
  assert.throws(() => createPreset('missing'));
});

test('transactions are atomic if a later command fails', () => {
  const s = store(),
    before = s.state.doc,
    revision = s.state.revision;
  assert.throws(() =>
    s.apply([
      { type: 'setDocument', name: 'name', value: 'Should roll back' },
      { type: 'setParameter', targetId: before.layers[0].id, name: 'material.opacity', value: 4 },
    ]),
  );
  assert.strictEqual(s.state.doc, before);
  assert.equal(s.state.revision, revision);
  assert.equal(s.state.undoCount, 0);
});

test('stale revisions and effects cannot overwrite current edits', () => {
  const s = store();
  s.apply([{ type: 'setDocument', name: 'name', value: 'Updated' }], { expectedRevision: 1 });
  assert.throws(
    () => s.apply([{ type: 'setDocument', name: 'name', value: 'Stale' }], { expectedRevision: 1 }),
    isCode('REVISION_CONFLICT'),
  );
  assert.throws(
    () =>
      s.apply([{ type: 'setDocument', name: 'name', value: 'Other' }], {
        effectId: 'other_effect',
      }),
    isCode('EFFECT_NOT_OPEN'),
  );
  assert.equal(s.state.doc.name, 'Updated');
});

test('idempotent retries create a single layer and reject conflicting payloads', () => {
  const s = store(),
    n = s.state.doc.layers.length;
  const command = [{ type: 'addLayer', kind: 'mesh', id: 'one_new_layer' }];
  const options = { expectedRevision: 1, requestId: 'one_request', effectId: s.state.doc.id };
  const first = s.apply(command, options),
    second = s.apply(command, options);
  assert.equal(second.replayed, true);
  assert.equal(first.transactionId, second.transactionId);
  assert.equal(s.state.doc.layers.length, n + 1);
  assert.equal(s.state.revision, 2);
  assert.throws(
    () => s.apply([{ type: 'addLayer', kind: 'light' }], options),
    isCode('IDEMPOTENCY_CONFLICT'),
  );
});

test('undo and redo restore values while revisions always increase', () => {
  const s = store(),
    original = s.state.doc.name;
  const result = s.apply([{ type: 'setDocument', name: 'name', value: 'Renamed' }]);
  s.undo(2, result.transactionId);
  assert.equal(s.state.doc.name, original);
  assert.equal(s.state.revision, 3);
  s.redo();
  assert.equal(s.state.doc.name, 'Renamed');
  assert.equal(s.state.revision, 4);
  assert.throws(() => s.undo(3), isCode('REVISION_CONFLICT'));
});

test('continuous UI changes coalesce into one reversible transaction', () => {
  const s = store(),
    id = s.state.doc.layers[0].id,
    value = s.state.doc.layers[0].material.intensity;
  s.apply([{ type: 'setParameter', targetId: id, name: 'material.intensity', value: 4 }], {
    group: id + ':intensity',
  });
  s.apply([{ type: 'setParameter', targetId: id, name: 'material.intensity', value: 5 }], {
    group: id + ':intensity',
  });
  assert.equal(s.state.undoCount, 1);
  s.undo();
  assert.equal(s.state.doc.layers[0].material.intensity, value);
});

test('undo refuses a non-latest transaction and restores a valid selection', () => {
  const s = store(),
    first = s.apply([{ type: 'addLayer', kind: 'light', id: 'added_light' }]);
  s.select('added_light');
  s.apply([{ type: 'setDocument', name: 'name', value: 'Later' }]);
  assert.throws(() => s.undo(s.state.revision, first.transactionId), isCode('HISTORY_CONFLICT'));
  s.undo();
  s.undo();
  assert.ok(s.state.doc.layers.some((l) => l.id === s.state.selectedId));
});

test('document and layer identities cannot be mutated through parameter paths', () => {
  const s = store();
  assert.throws(
    () => s.apply([{ type: 'setDocument', name: 'id', value: 'replacement' }]),
    isCode('INVALID_DOCUMENT_PATH'),
  );
  assert.throws(
    () =>
      s.apply([
        {
          type: 'setParameter',
          targetId: s.state.doc.layers[0].id,
          name: 'id',
          value: 'replacement',
        },
      ]),
    isCode('IMMUTABLE_ID'),
  );
  for (const path of [
    '__proto__.polluted',
    'constructor.prototype.polluted',
    'material.__proto__.polluted',
  ])
    assert.throws(() => setPath(s.state.doc, path, true));
  assert.equal((Object.prototype as Record<string, unknown>).polluted, undefined);
});

test('document cloning isolates mutable values and asset metadata', () => {
  const original = createPreset('arcane');
  original.assets.push({
    id: 'image',
    name: 'Image',
    type: 'texture',
    src: 'data:image/png;base64,AA==',
    bytes: 1,
    colorSpace: 'srgb',
  });
  const copy = cloneDocument(original);
  copy.assets[0].src = 'different';
  copy.layers[0].transform.position[0] = 55;
  copy.settings.exposure = 2;
  assert.equal(original.assets[0].src, 'data:image/png;base64,AA==');
  assert.notEqual(original.layers[0].transform.position[0], 55);
  assert.notEqual(original.settings.exposure, 2);
});

test('duplicate IDs, invalid times, missing resources and excessive slots are rejected', () => {
  const d = createPreset('arcane');
  assert.throws(() => parseDocument({ ...d, layers: [...d.layers, d.layers[0]] }));
  const missing = cloneDocument(d);
  missing.layers[0].material.textureId = 'missing';
  assert.throws(() => parseDocument(missing));
  const time = cloneDocument(d);
  time.layers[0].duration = d.duration + 1;
  assert.throws(() => parseDocument(time));
  const excessive = makeDocument();
  excessive.layers = Array.from({ length: 5 }, () => {
    const l = makeLayer('particles');
    l.emitter.count = 5000;
    return l;
  });
  assert.throws(() => parseDocument(excessive));
});

test('curve interpolation clamps endpoints and supports smooth and step evaluation', () => {
  const keys: [number, number][] = [
    [0, 0],
    [1, 1],
  ];
  assert.equal(evaluateCurve({ ...curve(keys), interpolation: 'linear' }, 0.25, 9), 0.25);
  assert.equal(evaluateCurve({ ...curve(keys), interpolation: 'smooth' }, 0.25, 9), 0.15625);
  assert.equal(evaluateCurve({ ...curve(keys), interpolation: 'step' }, 0.9, 9), 0);
  assert.equal(evaluateCurve(curve(keys), -1, 9), 0);
  assert.equal(evaluateCurve(curve(keys), 2, 9), 1);
  assert.equal(evaluateCurve(undefined, 0.5, 9), 9);
  assert.throws(() =>
    curveSchema.parse(
      curve([
        [0, 1],
        [0, 2],
      ]),
    ),
  );
  assert.throws(() =>
    curveSchema.parse(
      curve(
        [
          [0, 1],
          [1.1, 0],
        ],
        'life',
      ),
    ),
  );
});

test('curves only bind parameters evaluated by the runtime', () => {
  const s = store(),
    id = s.state.doc.layers[0].id;
  assert.ok(ANIMATABLE_PATHS.has('transform.scale.0'));
  s.apply([
    {
      type: 'setCurve',
      targetId: id,
      parameter: 'mesh.radius',
      curve: curve([
        [0, 0],
        [1, 2],
      ]),
    },
  ]);
  assert.throws(() =>
    s.apply([
      {
        type: 'setCurve',
        targetId: id,
        parameter: 'emitter.count',
        curve: curve([
          [0, 1],
          [1, 2],
        ]),
      },
    ]),
  );
  assert.throws(() =>
    s.apply([
      {
        type: 'setCurve',
        targetId: id,
        parameter: 'life.opacity',
        curve: curve(
          [
            [0, 1],
            [1, 2],
          ],
          'life',
        ),
      },
    ]),
  );
});

test('duration scaling preserves layer proportions and leaves life curves normalized', () => {
  const s = store(),
    before = s.state.doc,
    layer = before.layers.find((l) => l.kind === 'particles')!;
  s.apply([{ type: 'resizeDuration', duration: before.duration * 2 }]);
  const after = s.state.doc.layers.find((l) => l.id === layer.id)!;
  assert.equal(after.start, layer.start * 2);
  assert.equal(after.duration, layer.duration * 2);
  assert.equal(after.emitter.lifetime, layer.emitter.lifetime * 2);
  for (const [path, cv] of Object.entries(layer.curves))
    if (cv.domain === 'life') assert.deepEqual(after.curves[path], cv);
});

test('custom uniforms enforce metadata and allow scalar animation', () => {
  const s = store(),
    id = s.state.doc.layers[0].id;
  s.apply([
    { type: 'upsertShader', shader },
    { type: 'setParameter', targetId: id, name: 'material.shaderId', value: shader.id },
    { type: 'setUniform', targetId: id, name: 'uGain', value: 2 },
    {
      type: 'setCurve',
      targetId: id,
      parameter: 'uniforms.uGain',
      curve: curve([
        [0, 0.5],
        [1, 2],
      ]),
    },
  ]);
  assert.equal(s.state.doc.layers[0].material.uniforms.uGain, 2);
  assert.throws(
    () => s.apply([{ type: 'setUniform', targetId: id, name: 'uGain', value: 4 }]),
    isCode('UNIFORM_OUT_OF_RANGE'),
  );
  assert.throws(() =>
    s.apply([{ type: 'setUniform', targetId: id, name: 'uOffset', value: [1, 2] }]),
  );
  assert.throws(
    () => s.apply([{ type: 'setUniform', targetId: id, name: 'uUndeclared', value: 1 }]),
    isCode('UNIFORM_NOT_FOUND'),
  );
  s.apply([{ type: 'removeShader', targetId: shader.id }]);
  assert.equal(s.state.doc.layers[0].material.shaderId, '');
  assert.equal(s.state.doc.layers[0].curves['uniforms.uGain'], undefined);
});

test('shader profiles and reserved automatic uniform metadata are validated', () => {
  const d = createPreset('arcane');
  d.shaders = [{ ...shader, profile: 'particle' }];
  d.layers[0].material.shaderId = shader.id;
  assert.throws(() => parseDocument(d));
  d.shaders = [{ ...shader, uniforms: [{ name: 'uTime', type: 'float', default: 0 }] }];
  assert.throws(() => parseDocument(d));
});

test('particle layout is deterministic by seed and changes with a different seed', () => {
  const layer = makeLayer('particles'),
    a = generateParticles(layer, 5),
    b = generateParticles(layer, 5),
    c = generateParticles(layer, 6);
  for (const key of Object.keys(a) as (keyof typeof a)[]) assert.deepEqual(a[key], b[key]);
  assert.notDeepEqual(a.velocities, c.velocities);
});

test('analytic particles obey birth, lifetime, drag and gravity without stepping', () => {
  const layer = makeLayer('particles');
  Object.assign(layer.emitter, {
    count: 1,
    shape: 'point',
    emissionWindow: 0,
    lifetime: 3,
    lifetimeRandom: 0,
    speed: 2,
    speedRandom: 0,
    gravity: -2,
    drag: 0,
    turbulence: 0,
  });
  const data = generateParticles(layer, 7),
    sample = particleState(data, 0, layer, 1);
  assert.equal(particleState(data, 0, layer, -0.01).alive, false);
  assert.equal(particleState(data, 0, layer, 3.1).alive, false);
  assert.equal(sample.alive, true);
  assert.equal(sample.birth, 0);
  assert.ok(Math.abs(sample.position[1] - (data.velocities[1] * 2 - 1)) < 1e-7);
  assert.deepEqual(particleState(data, 0, layer, 1), sample);
});

test('continuous emitters reuse slots but cannot emit past the source window', () => {
  const l = makeLayer('particles');
  Object.assign(l.emitter, {
    count: 2,
    rate: 2,
    mode: 'continuous',
    emissionWindow: 1.1,
    lifetime: 0.8,
    lifetimeRandom: 0,
  });
  const d = generateParticles(l, 2);
  assert.equal(particleState(d, 0, l, 1.05).alive, true);
  assert.equal(particleState(d, 0, l, 2.05).alive, false);
});

test('runtime reuses pooled node resources without reactivating old public handles', async () => {
  const scene = new THREE.Scene(),
    runtime = new VfxRuntime(scene, new THREE.PerspectiveCamera(), {} as THREE.WebGLRenderer),
    doc = createPreset('arcane');
  await runtime.register(doc);
  const a = runtime.play(doc.id, { loop: false }),
    id = doc.layers.find((l) => l.kind === 'mesh')!.id,
    geometry = a.nodes.get(id)!.mesh!.geometry;
  runtime.update(doc.duration + 0.1);
  assert.equal(a.finished, true);
  assert.equal(a.root.children.length, 0);
  assert.equal(runtime.getMetrics().pooledInstances, 1);
  const b = runtime.play(doc.id);
  assert.notEqual(a.id, b.id);
  assert.strictEqual(b.nodes.get(id)!.mesh!.geometry, geometry);
  a.stop({ immediate: true });
  assert.equal(b.finished, false);
  runtime.stopAll();
  runtime.collect();
  runtime.unload(doc.id);
  assert.equal(runtime.getMetrics().pooledInstances, 0);
  assert.equal(scene.children.length, 0);
  runtime.dispose();
});

test('runtime caps its inactive pool and rejects unloading a live package', async () => {
  const runtime = new VfxRuntime(
      new THREE.Scene(),
      new THREE.PerspectiveCamera(),
      {} as THREE.WebGLRenderer,
    ),
    d = makeDocument();
  d.layers = [makeLayer('mesh')];
  await runtime.register(d);
  for (let i = 0; i < 20; i++) runtime.play(d.id, { loop: false });
  assert.throws(() => runtime.unload(d.id));
  runtime.update(d.duration + 0.1);
  assert.equal(runtime.instances.size, 0);
  assert.equal(runtime.getMetrics().pooledInstances, 16);
  runtime.unload(d.id);
  runtime.dispose();
});
