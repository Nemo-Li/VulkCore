/** Check the container before GLTFLoader can resolve any nested network resources. */
export function validateStaticGlb(bytes: ArrayBuffer) {
  if (bytes.byteLength < 20 || bytes.byteLength > 64 * 1024 * 1024)
    throw new Error('GLB 文件无效或超过 64 MiB');
  const view = new DataView(bytes),
    jsonSize = view.getUint32(12, true);
  if (
    view.getUint32(0, true) !== 0x46546c67 ||
    view.getUint32(4, true) !== 2 ||
    view.getUint32(8, true) !== bytes.byteLength ||
    view.getUint32(16, true) !== 0x4e4f534a ||
    jsonSize > bytes.byteLength - 20
  )
    throw new Error('需要有效的 glTF 2.0 二进制 GLB');
  const json = JSON.parse(new TextDecoder().decode(new Uint8Array(bytes, 20, jsonSize))) as {
    buffers?: { uri?: unknown }[];
    images?: { uri?: unknown }[];
    animations?: unknown[];
    skins?: unknown[];
    meshes?: { primitives?: { targets?: unknown[] }[] }[];
    extensionsRequired?: string[];
  };
  for (const resource of [...(json.buffers ?? []), ...(json.images ?? [])])
    if (
      resource.uri !== undefined &&
      (typeof resource.uri !== 'string' || !resource.uri.startsWith('data:'))
    )
      throw new Error('GLB 引用了外部文件，请将贴图与几何嵌入 GLB 后重新导入');
  if (
    json.animations?.length ||
    json.skins?.length ||
    json.meshes?.some((m) => m.primitives?.some((p) => p.targets?.length))
  )
    throw new Error('仅支持静态 GLB，请先烘焙骨骼、动画与 Morph 变形');
  if (
    json.extensionsRequired?.some((e) =>
      ['KHR_draco_mesh_compression', 'EXT_meshopt_compression', 'KHR_texture_basisu'].includes(e),
    )
  )
    throw new Error('请导出不依赖 Draco、Meshopt 或 KTX2 解码器的 GLB');
}
