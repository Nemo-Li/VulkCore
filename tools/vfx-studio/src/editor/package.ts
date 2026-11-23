import JSZip from 'jszip';
import { sha256Hex } from '../core/crypto';
import { cloneDocument, parseDocument, hashString, type EffectDocument } from '../core/model';
import {
  vertexMesh,
  vertexParticle,
  vertexPost,
  fragmentVfx,
  fragmentPost,
} from '../runtime/shaders';
export function bytesToBase64(data: Uint8Array): string {
  let s = '';
  for (let i = 0; i < data.length; i += 32768)
    s += String.fromCharCode(...data.subarray(i, i + 32768));
  return btoa(s);
}
export async function sha256(value: unknown): Promise<string> {
  return sha256Hex(new TextEncoder().encode(JSON.stringify(value)));
}
export async function freezeDocument(input: EffectDocument): Promise<EffectDocument> {
  const doc = cloneDocument(input);
  await Promise.all(
    doc.assets.map(async (a) => {
      if (a.src.startsWith('data:')) return;
      const r = await fetch(a.src);
      if (!r.ok) throw new Error(`资源冻结失败：${a.name}`);
      if (Number(r.headers.get('content-length')) > 67108864)
        throw new Error('单个资源不能超过 64 MiB');
      const bytes = new Uint8Array(await r.arrayBuffer());
      if (bytes.length > 67108864) throw new Error('单个资源不能超过 64 MiB');
      a.src = `data:${r.headers.get('content-type') ?? 'application/octet-stream'};base64,${bytesToBase64(bytes)}`;
      a.bytes = bytes.length;
    }),
  );
  return parseDocument(doc);
}
const safe = (s: string) =>
  s.replace(/[^\w.-]/g, '_').slice(0, 60) + '-' + hashString(s).toString(16);
export async function importProject(file: File): Promise<EffectDocument> {
  if (!file.name.toLowerCase().endsWith('.zip'))
    return freezeDocument(parseDocument(JSON.parse(await file.text())));
  const zip = await JSZip.loadAsync(await file.arrayBuffer());
  const manifest = zip.file('effect.json');
  if (!manifest) throw new Error('资源包缺少 effect.json');
  const doc = parseDocument(JSON.parse(await manifest.async('string')));
  for (const a of doc.assets) {
    if (a.src.startsWith('data:')) continue;
    const f = zip.file(a.src);
    if (!f) throw new Error(`资源包缺少 ${a.src}`);
    const data = await f.async('uint8array');
    const mime =
      a.type === 'model'
        ? 'model/gltf-binary'
        : a.src.endsWith('.webp')
          ? 'image/webp'
          : /\.jpe?g$/i.test(a.src)
            ? 'image/jpeg'
            : 'image/png';
    a.src = `data:${mime};base64,${bytesToBase64(data)}`;
    a.bytes = data.length;
  }
  for (const s of doc.shaders) {
    if (s.vertexPath) {
      const f = zip.file(s.vertexPath);
      if (!f) throw new Error(`缺少 ${s.vertexPath}`);
      s.vertex = await f.async('string');
      delete s.vertexPath;
    }
    if (s.fragmentPath) {
      const f = zip.file(s.fragmentPath);
      if (!f) throw new Error(`缺少 ${s.fragmentPath}`);
      s.fragment = await f.async('string');
      delete s.fragmentPath;
    }
  }
  return parseDocument(doc);
}
export async function exportPackage(input: EffectDocument, provenance: unknown) {
  const doc = await freezeDocument(input),
    zip = new JSZip();
  for (const a of doc.assets) {
    const response = await fetch(a.src),
      blob = await response.blob(),
      ext =
        a.type === 'model'
          ? 'glb'
          : blob.type.includes('jpeg')
            ? 'jpg'
            : blob.type.includes('webp')
              ? 'webp'
              : 'png';
    const path = `assets/${safe(a.id)}.${ext}`;
    zip.file(path, await blob.arrayBuffer());
    a.src = path;
  }
  for (const s of doc.shaders) {
    const name = safe(s.id);
    const vertex =
        s.vertex ||
        (s.profile === 'particle'
          ? vertexParticle
          : s.profile === 'post'
            ? vertexPost
            : vertexMesh),
      fragment = s.fragment || (s.profile === 'post' ? fragmentPost : fragmentVfx);
    s.vertexPath = `shaders/${name}.vert.glsl`;
    s.fragmentPath = `shaders/${name}.frag.glsl`;
    zip.file(s.vertexPath, vertex);
    zip.file(s.fragmentPath, fragment);
    s.vertex = '';
    s.fragment = '';
  }
  zip.file('effect.json', JSON.stringify(doc, null, 2));
  zip.file('index.html', demoHtml);
  zip.file(
    'README.md',
    `# ${doc.name}\n\n自包含 Three.js 游戏特效资源包。\n\n运行：在此目录执行 python3 -m http.server 5090，然后访问 http://127.0.0.1:5090/ 。\n\n游戏接入：使用自己的 Three.js 0.180.0 实例，import {VfxRuntime,VfxRenderer} from './vfx-runtime.js'；new VfxRuntime(scene,camera,renderer)，await vfx.preload('./effect.json')，vfx.play(id,{loop:false,position:[0,0,0]})；帧循环 vfx.update(dt)，pipeline.render()。统一输出管线已包含场景渲染，不需额外 renderer.render。\n\n类型：vfx-runtime.d.ts 从实际文档协议生成；TypeScript 游戏项目同时安装 three@0.180.0 与 @types/three@0.180.0。资源：effect.json 为版本化描述，shaders 为自定义源码，assets 为贴图和静态 GLB，verification.json 记录本次验证及运行时指纹。vendor 为本地 Three.js 依赖；THIRD_PARTY_NOTICES.txt 与 vendor/LICENSE-THREE.txt 保留第三方许可证。\n\n图层采用局部空间；静态 GLB 的原 PBR 材质由特效材质替代。自定义 Shader 使用 GLSL 3（引擎自动提供版本行）、对应顶点协议及统一线性颜色空间。\n`,
  );
  for (const path of [
    'runtime/vfx-runtime.js',
    'runtime/vfx-runtime.js.map',
    'runtime/vfx-runtime.d.ts',
    'runtime/THIRD_PARTY_NOTICES.txt',
    'runtime/runtime-manifest.json',
    'vendor/three.module.js',
    'vendor/three.core.js',
    'vendor/LICENSE-THREE.txt',
  ]) {
    const r = await fetch(new URL(path, document.baseURI));
    if (!r.ok) throw new Error(`运行时文件缺失：${path}。请重新构建特效编辑器。`);
    zip.file(path.startsWith('runtime/') ? path.slice(8) : path, await r.arrayBuffer());
  }
  const runtimeManifest = JSON.parse(await zip.file('runtime-manifest.json')!.async('string')) as {
    engineAbi: string;
    bundleSha256: string;
  };
  const runtimeHash = await sha256Hex(await zip.file('vfx-runtime.js')!.async('uint8array'));
  if (runtimeManifest.engineAbi !== doc.engineAbi || runtimeManifest.bundleSha256 !== runtimeHash)
    throw new Error('运行时版本或内容校验失败，请重新构建后导出');
  zip.file(
    'verification.json',
    JSON.stringify(
      {
        ...(provenance && typeof provenance === 'object' ? provenance : { details: provenance }),
        runtime: runtimeManifest,
      },
      null,
      2,
    ),
  );
  zip.file(
    'package.json',
    JSON.stringify(
      {
        name: '@ignis/vfx-runtime',
        version: '1.0.0',
        private: true,
        type: 'module',
        main: './vfx-runtime.js',
        types: './vfx-runtime.d.ts',
        exports: { '.': { types: './vfx-runtime.d.ts', import: './vfx-runtime.js' } },
        peerDependencies: { three: '0.180.0' },
      },
      null,
      2,
    ),
  );
  const bytes = await zip.generateAsync({
    type: 'uint8array',
    compression: 'DEFLATE',
    compressionOptions: { level: 6 },
  });
  return { bytes, filename: doc.name + '.ignis.zip', manifest: doc };
}
const demoHtml = `<!doctype html><html lang="zh-CN"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>VulkCore · 特效播放器</title><style>*{box-sizing:border-box}body{margin:0;background:#12161c;color:#e7edf4;font:14px system-ui}canvas{display:block;width:100vw;height:100vh}header{position:absolute;top:24px;left:30px}header small{color:#edae6c;letter-spacing:.2em}h1{font-size:24px;font-weight:500}footer{position:absolute;bottom:24px;left:30px;right:30px;display:flex;gap:12px;align-items:center;color:#929ba9}button{border:1px solid #404651;background:#222830;color:#eee;padding:9px 16px;border-radius:7px;cursor:pointer}#stats{margin-left:auto;font:12px monospace}#error{color:#ff9898;max-width:600px}</style><script type="importmap">{"imports":{"three":"./vendor/three.module.js"}}</script></head><body><canvas id="canvas"></canvas><header><small>VulkCore 特效播放器</small><h1 id="title">正在加载特效…</h1><p id="error"></p></header><footer><button id="replay">重新播放</button><button id="pause">暂停</button><button id="spawn">叠加实例</button><span>拖动旋转 · 滚轮缩放</span><span id="stats"></span></footer><script type="module">
import * as THREE from 'three';
import {VfxRuntime,VfxRenderer,createStage,OrbitControls} from './vfx-runtime.js';
const renderer=new THREE.WebGLRenderer({canvas:document.querySelector('#canvas'),antialias:false});const scene=new THREE.Scene();createStage(scene);const camera=new THREE.PerspectiveCamera(43,1,.05,150);camera.position.set(6,4.1,7.6);const orbit=new OrbitControls(camera,renderer.domElement);orbit.target.set(0,1,0);orbit.enableDamping=true;const runtime=new VfxRuntime(scene,camera,renderer),pipeline=new VfxRenderer(renderer,runtime);let instance,paused=false,id;
function resize(){const p=Math.min(devicePixelRatio,1.5);renderer.setPixelRatio(p);renderer.setSize(innerWidth,innerHeight);camera.aspect=innerWidth/innerHeight;camera.updateProjectionMatrix();pipeline.setSize(innerWidth*p,innerHeight*p);}addEventListener('resize',resize);resize();
try{id=await runtime.preload('./effect.json');instance=runtime.play(id,{loop:true});document.querySelector('#title').textContent=instance.doc.name;window.__VFX_DEMO__={runtime,pipeline,get instance(){return instance},loaded:true};}catch(e){document.querySelector('#error').textContent=String(e)+'；请通过本地 HTTP 服务打开，见 README.md';}
document.querySelector('#replay').onclick=()=>{runtime.stopAll();runtime.collect();instance=runtime.play(id,{loop:true});};document.querySelector('#pause').onclick=e=>{paused=!paused;e.target.textContent=paused?'继续':'暂停';};document.querySelector('#spawn').onclick=()=>runtime.play(id,{position:[(Math.random()-.5)*4,0,(Math.random()-.5)*3],loop:false});let previous=performance.now(),stamp=0;function frame(now){requestAnimationFrame(frame);if(!paused)runtime.update(Math.min(.1,(now-previous)/1000));previous=now;runtime.collect();orbit.update();pipeline.render();if(now-stamp>500){const m=pipeline.getMetrics();document.querySelector('#stats').textContent=m.activeParticles+' particles · '+m.drawCalls+' draws';stamp=now;}}requestAnimationFrame(frame);
</script></body></html>`;
