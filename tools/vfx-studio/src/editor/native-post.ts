import { fragmentPost } from '../runtime/shaders';

export function nativePostFragment(mode: string) {
  if (mode === 'custom') return fragmentPost;
  const operation = {
    flash: 'color += uTint * uStrength * uStrength;',
    chromatic: `vec2 shift = (vUv-.5) * uStrength * .03;
      color = vec3(texture(tDiffuse,vUv+shift).r,color.g,texture(tDiffuse,vUv-shift).b);`,
    distortion: `vec2 ndc = vUv*2.0-1.0;
      mat4 invProj = inverse(projectionMatrix);
      vec4 a = invProj*vec4(ndc,-1.0,1.0), b = invProj*vec4(ndc,1.0,1.0);
      vec3 rayStart = a.xyz/a.w, rayEnd = b.xyz/b.w;
      vec3 hit = mix(rayStart,rayEnd,(uPostCenter.z-rayStart.z)/(rayEnd.z-rayStart.z));
      vec2 p = (hit.xy-uPostCenter.xy)/max(.0001,uPostRadius);
      float r = length(p);
      float ring = exp(-pow((r-mix(.04,.95,uProgress))*18.0,2.0));
      float mask = ring*(1.0-smoothstep(.9,1.0,r));
      if(uDepthEnabled>.5){
        float d = texture(uSceneDepth,vUv).r;
        vec4 depth = invProj*vec4(ndc,d*2.0-1.0,1.0);
        mask *= clamp((-depth.z/depth.w+hit.z)*4.0,0.0,1.0);
      }
      if(hit.z<0.0) color = texture(tDiffuse,clamp(vUv+normalize(p+vec2(.0001))*mask*uStrength*.12,vec2(.001),vec2(.999))).rgb;`,
  }[mode];
  return `
in vec2 vUv;
out vec4 outColor;
uniform sampler2D tDiffuse,uSceneDepth;
uniform vec3 uTint,uPostCenter;
uniform float uStrength,uProgress,uDepthEnabled,uPostRadius;
void main(){
  vec3 color = texture(tDiffuse,vUv).rgb;
  ${operation}
  outColor=vec4(color,1.0);
}`;
}
