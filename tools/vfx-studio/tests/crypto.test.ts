import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { randomUUID, sha256Hex } from '../src/core/crypto';

const messages = ['', 'abc', '局域网特效编辑器', 'a'.repeat(1000000)];
async function checkHashes() {
  for (const message of messages) {
    const expected = createHash('sha256').update(message).digest('hex');
    assert.equal(await sha256Hex(new TextEncoder().encode(message)), expected);
  }
}
test('SHA-256 content hashes agree with Node using native Web Crypto', async () => {
  await checkHashes();
});
test('plain HTTP fallback keeps SHA-256 keys and random UUIDs valid without secure APIs', async () => {
  const descriptor = Object.getOwnPropertyDescriptor(globalThis, 'crypto')!;
  const native = globalThis.crypto;
  Object.defineProperty(globalThis, 'crypto', {
    configurable: true,
    value: { getRandomValues: native.getRandomValues.bind(native) },
  });
  try {
    await checkHashes();
    const ids = Array.from({ length: 1000 }, () => randomUUID());
    assert.equal(new Set(ids).size, ids.length);
    for (const id of ids)
      assert.match(id, /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/);
  } finally {
    Object.defineProperty(globalThis, 'crypto', descriptor);
  }
});
