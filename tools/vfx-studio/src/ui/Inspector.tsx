import { useState } from 'react';
import { Braces, SlidersHorizontal, Settings2, ChevronRight } from 'lucide-react';
import { ANIMATABLE_PATHS, type Layer, getPath, uid } from '../core/model';
import { EditorStore } from '../core/store';
import {
  useEditor,
  Section,
  NumberField,
  SelectField,
  ColorField,
  ToggleField,
  VectorField,
  TextField,
  apply,
} from './controls';
import { curve } from '../core/presets';
export function Inspector({
  store,
  onCurve,
  onShader,
}: {
  store: EditorStore;
  onCurve: (path: string) => void;
  onShader: () => void;
}) {
  const { doc, selectedId } = useEditor(store);
  const layer = doc.layers.find((l) => l.id === selectedId);
  const [tab, setTab] = useState<'layer' | 'material' | 'global'>('layer');
  const set = (name: string, value: unknown) =>
    layer &&
    apply(
      store,
      [{ type: 'setParameter', targetId: layer.id, name, value }],
      `调整 ${name}`,
      layer.id + name,
    );
  const docSet = (name: string, value: unknown) =>
    apply(store, [{ type: 'setDocument', name, value }], `调整 ${name}`, name);
  const numeric = (
    label: string,
    path: string,
    min = 0,
    max = 10,
    step = 0.01,
    unit = '',
    slider = false,
  ) =>
    layer && (
      <NumberField
        key={path}
        label={label}
        value={Number(getPath(layer, path))}
        min={min}
        max={max}
        step={step}
        unit={unit}
        slider={slider}
        onChange={(v) => set(path, v)}
        curve={Boolean(layer.curves[path])}
        onCurve={ANIMATABLE_PATHS.has(path) ? () => onCurve(path) : undefined}
      />
    );
  return (
    <aside className="inspector-panel">
      <div className="panel-heading">
        <span>检查器</span>
        <SlidersHorizontal size={14} />
      </div>
      <div className="panel-tabs">
        <button className={tab === 'layer' ? 'selected' : ''} onClick={() => setTab('layer')}>
          属性
        </button>
        <button className={tab === 'material' ? 'selected' : ''} onClick={() => setTab('material')}>
          材质
        </button>
        <button className={tab === 'global' ? 'selected' : ''} onClick={() => setTab('global')}>
          全局
        </button>
      </div>
      <div className="inspector-scroll">
        {tab === 'global' || !layer ? (
          <>
            <div className="inspector-object">
              <div className="object-icon">
                <Settings2 size={18} />
              </div>
              <div>
                <strong>效果设置</strong>
                <small>项目与统一渲染管线</small>
              </div>
            </div>
            <Section title="项目">
              <TextField label="项目名称" value={doc.name} onChange={(v) => docSet('name', v)} />
              <NumberField
                label="效果时长"
                value={doc.duration}
                min={0.1}
                max={120}
                unit="s"
                step={0.1}
                onChange={(duration) =>
                  apply(store, [{ type: 'resizeDuration', duration }], '缩放效果时长')
                }
              />
              <NumberField
                label="随机种子"
                value={doc.seed}
                min={0}
                max={4294967295}
                step={1}
                onChange={(v) => docSet('seed', Math.round(v))}
              />
              <ToggleField
                label="循环播放"
                value={doc.loop}
                onChange={(v) => {
                  store.playback.loop = v;
                  docSet('loop', v);
                }}
              />
            </Section>
            <Section title="Bloom · 辉光">
              <ToggleField
                label="启用辉光"
                value={doc.settings.bloom}
                onChange={(v) => docSet('settings.bloom', v)}
              />
              <NumberField
                label="辉光强度"
                value={doc.settings.bloomStrength}
                min={0}
                max={3}
                slider
                onChange={(v) => docSet('settings.bloomStrength', v)}
              />
              <NumberField
                label="亮度阈值"
                value={doc.settings.bloomThreshold}
                min={0}
                max={4}
                slider
                onChange={(v) => docSet('settings.bloomThreshold', v)}
              />
              <NumberField
                label="扩散半径"
                value={doc.settings.bloomRadius}
                min={0}
                max={1}
                slider
                onChange={(v) => docSet('settings.bloomRadius', v)}
              />
            </Section>
            <Section title="显示与质量">
              <NumberField
                label="曝光"
                value={doc.settings.exposure}
                min={0.1}
                max={4}
                slider
                onChange={(v) => docSet('settings.exposure', v)}
              />
              <NumberField
                label="暗角"
                value={doc.settings.vignette}
                min={0}
                max={1}
                slider
                onChange={(v) => docSet('settings.vignette', v)}
              />
              <NumberField
                label="色散"
                value={doc.settings.chromatic}
                min={0}
                max={0.05}
                step={0.001}
                onChange={(v) => docSet('settings.chromatic', v)}
              />
              <SelectField
                label="渲染倍率"
                value={String(doc.settings.resolutionScale)}
                options={['0.5', '0.75', '1', '1.25', '1.5', '2']}
                onChange={(v) => docSet('settings.resolutionScale', Number(v))}
              />
            </Section>
          </>
        ) : (
          <>
            <div className="inspector-object">
              <div className="object-icon" style={{ color: layer.material.color }}>
                <KindSymbol layer={layer} />
              </div>
              <div>
                <strong>{layer.name}</strong>
                <small>{layer.kind.toUpperCase()} · LOCAL SPACE</small>
              </div>
            </div>
            {tab === 'layer' ? (
              <>
                <Section title="基础">
                  <TextField label="图层名称" value={layer.name} onChange={(v) => set('name', v)} />
                  {numeric(
                    '开始时间',
                    'start',
                    0,
                    Math.max(0, doc.duration - layer.duration),
                    0.01,
                    's',
                  )}
                  {numeric(
                    '持续时间',
                    'duration',
                    0.02,
                    Math.max(0.02, doc.duration - layer.start),
                    0.01,
                    's',
                  )}
                  <ToggleField
                    label="启用图层"
                    value={layer.enabled}
                    onChange={(v) => set('enabled', v)}
                  />
                </Section>
                <Section title="变换">
                  <VectorField
                    label="位置"
                    values={layer.transform.position}
                    onChange={(v) => set('transform.position', v)}
                  />
                  <VectorField
                    label="旋转 °"
                    values={layer.transform.rotation}
                    rotation
                    onChange={(v) => set('transform.rotation', v)}
                  />
                  <VectorField
                    label="缩放"
                    values={layer.transform.scale}
                    onChange={(v) => set('transform.scale', v)}
                  />
                </Section>
                {layer.kind === 'particles' && (
                  <>
                    <Section title="发射器" badge={`${layer.emitter.count} slots`}>
                      <SelectField
                        label="发射模式"
                        value={layer.emitter.mode}
                        options={[
                          { value: 'burst', label: '单次爆发' },
                          { value: 'continuous', label: '持续发射' },
                        ]}
                        onChange={(v) => set('emitter.mode', v)}
                      />
                      {numeric('粒子槽位', 'emitter.count', 1, 5000, 1)}
                      {layer.emitter.mode === 'continuous' &&
                        numeric('每秒发射', 'emitter.rate', 1, 2000, 1)}
                      {numeric('发射窗口', 'emitter.emissionWindow', 0, doc.duration, 0.01, 's')}
                      {numeric('粒子寿命', 'emitter.lifetime', 0.02, 30, 0.01, 's')}
                      {numeric('寿命随机', 'emitter.lifetimeRandom', 0, 0.95, 0.01, '', true)}
                      <SelectField
                        label="发射形状"
                        value={layer.emitter.shape}
                        options={[
                          { value: 'point', label: '点' },
                          { value: 'sphere', label: '球体' },
                          { value: 'cone', label: '锥体' },
                          { value: 'ring', label: '圆环' },
                          { value: 'box', label: '盒体' },
                        ]}
                        onChange={(v) => set('emitter.shape', v)}
                      />
                      {numeric('发射半径', 'emitter.radius', 0, 10, 0.01, 'm')}
                      {layer.emitter.shape === 'cone' &&
                        numeric('锥体角度', 'emitter.angle', 0, 180, 1, '°')}
                    </Section>
                    <Section title="运动" initialOpen={false}>
                      {numeric('初始速度', 'emitter.speed', 0, 50, 0.1, 'm/s')}
                      {numeric('速度随机', 'emitter.speedRandom', 0, 1, 0.01, '', true)}
                      {numeric('重力加速度', 'emitter.gravity', -30, 30, 0.1)}
                      {numeric('阻力', 'emitter.drag', 0, 20, 0.1)}
                      {numeric('湍流', 'emitter.turbulence', 0, 10, 0.01)}
                      {numeric('旋转速度', 'emitter.rotationSpeed', -20, 20, 0.1, 'rad/s')}
                    </Section>
                    <Section title="形态" initialOpen={false}>
                      {numeric('粒子尺寸', 'emitter.size', 0.001, 10, 0.01, 'm')}
                      {numeric('尺寸随机', 'emitter.sizeRandom', 0, 0.95, 0.01, '', true)}
                      {numeric('拉伸倍率', 'emitter.stretch', 0.1, 15, 0.1)}
                      <button
                        className="secondary-button full-width"
                        onClick={() => onCurve('life.size')}
                      >
                        尺寸随寿命变化 <ChevronRight size={13} />
                      </button>
                      <button
                        className="secondary-button full-width"
                        onClick={() => onCurve('life.opacity')}
                      >
                        透明度随寿命变化 <ChevronRight size={13} />
                      </button>
                    </Section>
                  </>
                )}
                {layer.kind === 'mesh' && (
                  <Section title="几何体">
                    <SelectField
                      label="网格形状"
                      value={layer.mesh.shape}
                      options={[
                        { value: 'sphere', label: '球体' },
                        { value: 'plane', label: '平面' },
                        { value: 'ring', label: '圆环' },
                        { value: 'torus', label: '圆环体' },
                        { value: 'cone', label: '锥体' },
                        { value: 'cylinder', label: '圆柱' },
                        {
                          value: 'model',
                          label: '导入 GLB',
                          disabled: !doc.assets.some((a) => a.type === 'model'),
                        },
                      ]}
                      onChange={(v) => {
                        if (v === 'model') {
                          const model = doc.assets.find((a) => a.type === 'model');
                          if (model)
                            apply(
                              store,
                              [
                                {
                                  type: 'setParameter',
                                  targetId: layer.id,
                                  name: 'mesh.shape',
                                  value: v,
                                },
                                {
                                  type: 'setParameter',
                                  targetId: layer.id,
                                  name: 'mesh.modelId',
                                  value: model.id,
                                },
                                {
                                  type: 'setParameter',
                                  targetId: layer.id,
                                  name: 'mesh.radius',
                                  value: 1,
                                },
                              ],
                              '使用模型',
                            );
                        } else set('mesh.shape', v);
                      }}
                    />
                    {layer.mesh.shape === 'plane' ? (
                      <>
                        {numeric('平面宽度', 'mesh.width', 0.001, 50, 0.05, 'm')}
                        {numeric('平面高度', 'mesh.height', 0.001, 50, 0.05, 'm')}
                      </>
                    ) : (
                      numeric(
                        layer.mesh.shape === 'model' ? '模型倍率' : '网格半径',
                        'mesh.radius',
                        0.001,
                        50,
                        0.01,
                        layer.mesh.shape === 'model' ? '×' : 'm',
                      )
                    )}
                    {['cone', 'cylinder'].includes(layer.mesh.shape) &&
                      numeric('几何高度', 'mesh.height', 0.001, 50, 0.05, 'm')}
                    {layer.mesh.shape === 'torus' &&
                      numeric('管线厚度', 'mesh.thickness', 0.001, 10, 0.01, 'm')}
                    {['plane', 'ring', 'torus'].includes(layer.mesh.shape) && (
                      <SelectField
                        label="平面朝向"
                        value={layer.mesh.orientation}
                        options={[
                          { value: 'horizontal', label: '水平 XZ' },
                          { value: 'vertical', label: '竖直 XY' },
                        ]}
                        onChange={(v) => set('mesh.orientation', v)}
                      />
                    )}
                    {layer.mesh.shape === 'model' && (
                      <SelectField
                        label="模型资源"
                        value={layer.mesh.modelId}
                        options={doc.assets
                          .filter((a) => a.type === 'model')
                          .map((a) => ({ value: a.id, label: a.name }))}
                        onChange={(v) => set('mesh.modelId', v)}
                      />
                    )}
                    <ToggleField
                      label="线框显示"
                      value={layer.mesh.wireframe}
                      onChange={(v) => set('mesh.wireframe', v)}
                    />
                  </Section>
                )}
                {layer.kind === 'ribbon' && (
                  <Section title="拖尾路径">
                    <SelectField
                      label="路径类型"
                      value={layer.ribbon.path}
                      options={[
                        { value: 'spiral', label: '螺旋' },
                        { value: 'arc', label: '弧线' },
                        { value: 'bolt', label: '闪电' },
                        { value: 'orbit', label: '环绕' },
                      ]}
                      onChange={(v) => set('ribbon.path', v)}
                    />
                    {numeric('路径半径', 'ribbon.radius', 0.01, 30, 0.05, 'm')}
                    {numeric('路径高度', 'ribbon.height', -20, 20, 0.05, 'm')}
                    {numeric('拖尾宽度', 'ribbon.width', 0.001, 3, 0.005, 'm')}
                    {numeric('缠绕圈数', 'ribbon.turns', 0.1, 10, 0.1)}
                    {numeric('运动速度', 'ribbon.speed', 0, 10, 0.1)}
                    {numeric('拖尾长度', 'ribbon.tail', 0.01, 1, 0.01, '', true)}
                    {numeric('路径扰动', 'ribbon.jitter', 0, 5, 0.01)}
                    {numeric('路径细分', 'ribbon.segments', 8, 256, 1)}
                  </Section>
                )}
                {layer.kind === 'light' && (
                  <Section title="局部照明">
                    <ColorField
                      label="灯光颜色"
                      value={layer.material.color}
                      onChange={(v) => set('material.color', v)}
                    />
                    {numeric('灯光强度', 'light.intensity', 0, 100, 0.1)}
                    {numeric('影响距离', 'light.distance', 0.1, 100, 0.1, 'm')}
                    {numeric('衰减指数', 'light.decay', 0, 4, 0.1)}
                  </Section>
                )}
                {layer.kind === 'postprocess' && (
                  <Section title="后处理贡献">
                    <SelectField
                      label="效果类型"
                      value={layer.post.mode}
                      options={[
                        { value: 'distortion', label: '局部空间扭曲' },
                        { value: 'flash', label: '全屏闪光' },
                        { value: 'chromatic', label: '色散脉冲' },
                        { value: 'custom', label: '自定义 Pass' },
                      ]}
                      onChange={(v) => set('post.mode', v)}
                    />
                    {numeric('贡献强度', 'post.strength', 0, 3, 0.01)}
                    {['distortion', 'custom'].includes(layer.post.mode) &&
                      numeric('遮罩半径', 'post.radius', 0.01, 30, 0.05, 'm')}
                    {layer.post.mode === 'flash' && (
                      <ColorField
                        label="闪光颜色"
                        value={layer.material.color}
                        onChange={(v) => set('material.color', v)}
                      />
                    )}
                    {layer.post.mode === 'custom' && (
                      <SelectField
                        label="执行阶段"
                        value={layer.post.stage}
                        options={[
                          { value: 'beforeBloom', label: '辉光之前' },
                          { value: 'afterBloom', label: '辉光之后' },
                        ]}
                        onChange={(v) => set('post.stage', v)}
                      />
                    )}
                    {layer.post.mode === 'custom' && (
                      <>
                        <SelectField
                          label="Pass Shader"
                          value={layer.post.shaderId}
                          options={[
                            { value: '', label: '请选择 Shader' },
                            ...doc.shaders
                              .filter((s) => s.profile === 'post')
                              .map((s) => ({ value: s.id, label: s.name })),
                          ]}
                          onChange={(v) => set('post.shaderId', v)}
                        />
                        <button className="secondary-button full-width" onClick={onShader}>
                          <Braces size={14} /> 编写后处理 Shader
                        </button>
                      </>
                    )}
                  </Section>
                )}
              </>
            ) : layer.kind === 'light' ? (
              <Section title="灯光颜色">
                <ColorField
                  label="灯光颜色"
                  value={layer.material.color}
                  onChange={(v) => set('material.color', v)}
                />
                <p className="muted-text">灯光强度、影响距离和衰减位于属性页。</p>
              </Section>
            ) : layer.kind === 'postprocess' ? (
              <Section title="后处理材质">
                {layer.post.mode === 'custom' ? (
                  <>
                    <ColorField
                      label="主色输入"
                      value={layer.material.color}
                      onChange={(v) => set('material.color', v)}
                    />
                    <ColorField
                      label="辅助色输入"
                      value={layer.material.secondaryColor}
                      onChange={(v) => set('material.secondaryColor', v)}
                    />
                    {numeric('强度输入', 'material.intensity', 0, 20, 0.1)}
                    {numeric('透明度输入', 'material.opacity', 0, 1, 0.01)}
                    <SelectField
                      label="颜色贴图输入"
                      value={layer.material.textureId}
                      options={[
                        { value: '', label: '默认白色' },
                        ...doc.assets
                          .filter((a) => a.type === 'texture')
                          .map((a) => ({ value: a.id, label: a.name })),
                      ]}
                      onChange={(v) => set('material.textureId', v)}
                    />
                    <p className="muted-text">
                      自定义源码决定如何使用这些输入；Pass、遮罩和执行阶段位于属性页。
                    </p>
                  </>
                ) : (
                  <p className="muted-text">在属性页调整扭曲、闪光或色散的强度与时序。</p>
                )}
                <button className="secondary-button full-width" onClick={onShader}>
                  <Braces size={14} /> 打开后处理 Shader 工作台
                </button>
              </Section>
            ) : (
              <>
                <Section title="表面着色">
                  <SelectField
                    label="内置着色"
                    value={layer.material.style}
                    options={[
                      { value: 'glow', label: '柔和发光' },
                      { value: 'smoke', label: '程序烟雾' },
                      { value: 'fire', label: '程序火焰' },
                      { value: 'ring', label: '扩散光环' },
                      { value: 'spark', label: '高亮火花' },
                      { value: 'rune', label: '符文法阵' },
                      { value: 'electric', label: '能量电弧' },
                      { value: 'shield', label: '菲涅耳护盾' },
                    ]}
                    onChange={(v) => set('material.style', v)}
                  />
                  <ColorField
                    label="主色"
                    value={layer.material.color}
                    onChange={(v) => set('material.color', v)}
                  />
                  <ColorField
                    label="辅助色"
                    value={layer.material.secondaryColor}
                    onChange={(v) => set('material.secondaryColor', v)}
                  />
                  {numeric('发光强度', 'material.intensity', 0, 20, 0.1, '', true)}
                  {numeric('透明度', 'material.opacity', 0, 1, 0.01, '', true)}
                  <SelectField
                    label="混合方式"
                    value={layer.material.blending}
                    options={[
                      { value: 'add', label: '叠加发光' },
                      { value: 'normal', label: '透明混合' },
                    ]}
                    onChange={(v) => set('material.blending', v)}
                  />
                  <ToggleField
                    label="写入深度"
                    value={layer.material.depthWrite}
                    onChange={(v) => set('material.depthWrite', v)}
                  />
                  {numeric('交界柔化', 'material.softness', 0, 5, 0.01, 'm')}
                </Section>
                <Section title="纹理与自定义 Shader">
                  <SelectField
                    label="颜色贴图"
                    value={layer.material.textureId}
                    options={[
                      { value: '', label: '程序生成' },
                      ...doc.assets
                        .filter((a) => a.type === 'texture')
                        .map((a) => ({ value: a.id, label: a.name })),
                    ]}
                    onChange={(v) => set('material.textureId', v)}
                  />
                  <SelectField
                    label="Shader"
                    value={layer.material.shaderId}
                    options={[
                      { value: '', label: '内置 Shader' },
                      ...doc.shaders
                        .filter(
                          (s) => s.profile === (layer.kind === 'particles' ? 'particle' : 'mesh'),
                        )
                        .map((s) => ({ value: s.id, label: s.name })),
                    ]}
                    onChange={(v) => set('material.shaderId', v)}
                  />
                  <button className="secondary-button full-width" onClick={onShader}>
                    <Braces size={14} /> 打开 Shader 工作台
                  </button>
                </Section>
              </>
            )}
            {(() => {
              const shader = doc.shaders.find(
                (s) =>
                  s.id ===
                  (layer.kind === 'postprocess'
                    ? layer.post.mode === 'custom'
                      ? layer.post.shaderId
                      : ''
                    : layer.material.shaderId),
              );
              if (!shader?.uniforms.length) return null;
              return (
                <Section title="Shader 参数">
                  {shader.uniforms.map((u) => {
                    const value = layer.material.uniforms[u.name] ?? u.default;
                    const change = (v: unknown) =>
                      apply(
                        store,
                        [{ type: 'setUniform', targetId: layer.id, name: u.name, value: v }],
                        `修改 ${u.name}`,
                        layer.id + u.name,
                      );
                    if (u.type === 'float')
                      return (
                        <NumberField
                          key={u.name}
                          label={u.label ?? u.name}
                          value={Number(value)}
                          min={u.min ?? -1000}
                          max={u.max ?? 1000}
                          onChange={change}
                          curve={Boolean(layer.curves['uniforms.' + u.name])}
                          onCurve={() => onCurve('uniforms.' + u.name)}
                        />
                      );
                    if (u.type === 'color')
                      return (
                        <ColorField
                          key={u.name}
                          label={u.label ?? u.name}
                          value={String(value)}
                          onChange={change}
                        />
                      );
                    if (u.type === 'texture')
                      return (
                        <SelectField
                          key={u.name}
                          label={u.label ?? u.name}
                          value={String(value)}
                          options={[
                            { value: '', label: '默认白色' },
                            ...doc.assets
                              .filter((a) => a.type === 'texture')
                              .map((a) => ({ value: a.id, label: a.name })),
                          ]}
                          onChange={change}
                        />
                      );
                    return (
                      <div key={u.name}>
                        {(value as number[]).map((n, i) => (
                          <NumberField
                            key={i}
                            label={`${u.name}.${i}`}
                            value={n}
                            onChange={(v) => {
                              const next = [...(value as number[])];
                              next[i] = v;
                              change(next);
                            }}
                          />
                        ))}
                      </div>
                    );
                  })}
                </Section>
              );
            })()}
          </>
        )}
      </div>
      <div className="inspector-foot">
        <span className="tiny-dot" /> 参数修改实时预览 · 米 / 秒
      </div>
    </aside>
  );
}
function KindSymbol({ layer }: { layer: Layer }) {
  return (
    <span className="kind-symbol">
      {{ particles: '✧', mesh: '◇', ribbon: '⌁', light: '☼', postprocess: '◉' }[layer.kind]}
    </span>
  );
}
