import { build, context } from 'esbuild';
import { mkdir, copyFile, readFile, writeFile } from 'node:fs/promises';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { createHash } from 'node:crypto';
const run = promisify(execFile);
await mkdir('public/runtime', { recursive: true });
await mkdir('public/vendor', { recursive: true });
async function metadata() {
  await run(process.execPath, ['node_modules/tsx/dist/cli.mjs', 'scripts/build-types.ts']);
  for (const file of ['three.module.js', 'three.core.js'])
    await copyFile('node_modules/three/build/' + file, 'public/vendor/' + file);
  const three = await readFile('node_modules/three/LICENSE', 'utf8'),
    zod = await readFile('node_modules/zod/LICENSE', 'utf8'),
    noble = await readFile('node_modules/@noble/hashes/LICENSE', 'utf8');
  await writeFile(
    'public/runtime/THIRD_PARTY_NOTICES.txt',
    'Three.js 0.180.0 (core and bundled addons)\n\n' +
      three +
      '\n\nZod 4 (bundled document validation)\n\n' +
      zod +
      '\n\n@noble/hashes (SHA-256 fallback for LAN HTTP)\n\n' +
      noble,
  );
  await copyFile('node_modules/three/LICENSE', 'public/vendor/LICENSE-THREE.txt');
  const bundle = await readFile('public/runtime/vfx-runtime.js');
  await writeFile(
    'public/runtime/runtime-manifest.json',
    JSON.stringify(
      {
        version: '1.0.0',
        engineAbi: 'ignis-webgl2-v1',
        threeVersion: '0.180.0',
        builtAt: new Date().toISOString(),
        bundleSha256: createHash('sha256').update(bundle).digest('hex'),
        bundleBytes: bundle.length,
      },
      null,
      2,
    ),
  );
}
const options = {
  entryPoints: ['src/runtime/index.ts'],
  bundle: true,
  format: 'esm',
  target: 'es2022',
  outfile: 'public/runtime/vfx-runtime.js',
  sourcemap: true,
  minify: true,
  plugins: [
    {
      name: 'shared-three-core',
      setup(api) {
        api.onResolve({ filter: /^three$/ }, (args) => ({ path: args.path, external: true }));
      },
    },
    {
      name: 'runtime-metadata',
      setup(api) {
        api.onEnd(async (result) => {
          if (!result.errors.length) await metadata();
        });
      },
    },
  ],
};
if (process.argv.includes('--watch')) {
  const watcher = await context(options);
  await watcher.watch();
  console.log('Watching shared runtime source and TypeScript declarations');
} else {
  await build(options);
  console.log('Standalone runtime, TypeScript declarations and local Three.js vendor ready');
}
