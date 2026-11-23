export const vertexMesh = /* glsl */ `
precision highp float;
out vec2 vUv; out float vLife; out float vRandom; out float vAlive;
out vec3 vWorld; out vec3 vNormal; out vec3 vViewDir; out float vViewDepth;
uniform float uProgress;
void main(){
 vUv=uv; vLife=uProgress; vRandom=0.5; vAlive=1.0;
 vec4 world=modelMatrix*vec4(position,1.0); vec4 view=viewMatrix*world;
 vWorld=world.xyz; vNormal=normalize(normalMatrix*normal); vViewDir=normalize(-view.xyz); vViewDepth=-view.z;
 gl_Position=projectionMatrix*view;
}`;
export const vertexParticle = /* glsl */ `
precision highp float;
in vec3 aPosition; in vec3 aVelocity; in float aStart; in float aLifetime; in float aSize; in float aRotation; in float aRandom;
uniform float uTime,uProgress,uSpeed,uGravity,uDrag,uTurbulence,uSize,uStretch,uRotationSpeed,uEmitterMode,uCycle,uEmissionWindow;
uniform vec3 uCameraRight,uCameraUp; uniform float uWorldScale; uniform sampler2D uLifeCurve;
out vec2 vUv; out float vLife; out float vRandom; out float vAlive;
out vec3 vWorld; out vec3 vNormal; out vec3 vViewDir; out float vViewDepth;
void main(){
 float birth=aStart; float raw=uTime-birth;
 if(uEmitterMode>0.5&&raw>=0.0)birth+=floor(raw/max(uCycle,0.001))*uCycle;
 float age=max(0.0,uTime-birth); float life=clamp(age/aLifetime,0.0,1.0);
 float alive=step(0.0,uTime-birth)*step(age,aLifetime)*step(birth,uEmissionWindow);
 vec4 cv=texture(uLifeCurve,vec2(life,0.5));
 float travel=uDrag>0.001?(1.0-exp(-uDrag*age))/uDrag:age;
 vec3 offset=aVelocity*uSpeed*travel+vec3(0.0,uGravity*age*age*0.5,0.0);
 offset+=sin(vec3(1.7,2.1,1.3)*age+aRandom*25.0+vec3(0.0,2.0,4.0))*uTurbulence*age;
 vec4 center=modelMatrix*vec4(aPosition+offset,1.0);
 float rotation=aRotation+age*uRotationSpeed;
 mat2 rot=mat2(cos(rotation),-sin(rotation),sin(rotation),cos(rotation));
 vec2 quad=rot*(position.xy*vec2(1.0,uStretch));
 float size=aSize*uSize*cv.r*uWorldScale*alive;
 vec3 world=center.xyz+(uCameraRight*quad.x+uCameraUp*quad.y)*size;
 vec4 view=viewMatrix*vec4(world,1.0);
 vUv=uv;vLife=life;vRandom=aRandom;vAlive=alive*cv.g;vWorld=world;vNormal=vec3(0.0,0.0,1.0);vViewDir=vec3(0.0,0.0,1.0);vViewDepth=-view.z;
 gl_Position=projectionMatrix*view;
}`;
const noise = /* glsl */ `
float hash3(vec3 p){p=fract(p*0.3183099+vec3(.1,.2,.3));p*=17.0;return fract(p.x*p.y*p.z*(p.x+p.y+p.z));}
float noise3(vec3 p){vec3 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);return mix(mix(mix(hash3(i),hash3(i+vec3(1,0,0)),f.x),mix(hash3(i+vec3(0,1,0)),hash3(i+vec3(1,1,0)),f.x),f.y),mix(mix(hash3(i+vec3(0,0,1)),hash3(i+vec3(1,0,1)),f.x),mix(hash3(i+vec3(0,1,1)),hash3(i+vec3(1,1,1)),f.x),f.y),f.z);}
float fbm(vec3 p){float v=0.0,a=.5;for(int i=0;i<4;i++){v+=a*noise3(p);p=p*2.03+1.7;a*=.5;}return v;}
`;
export const fragmentVfx = /* glsl */ `
precision highp float;
in vec2 vUv;in float vLife;in float vRandom;in float vAlive;
in vec3 vWorld;in vec3 vNormal;in vec3 vViewDir;in float vViewDepth;
uniform vec3 uTint,uSecondary;uniform float uIntensity,uOpacity,uTime,uProgress,uStyle,uIsRibbon,uSoftness,uDepthEnabled,uHasTexture;
uniform sampler2D uTexture,uSceneDepth;uniform vec2 uResolution,uNearFar;
out vec4 outColor;
${noise}
float line(float x,float w){return exp(-abs(x)/max(w,.0001));}
void main(){
 if(vAlive<0.001)discard;
 vec2 p=vUv*2.0-1.0;float r=length(p),a=atan(p.y,p.x);float alpha=0.0;vec3 col=uTint;
 if(uIsRibbon>0.5){alpha=pow(max(0.0,1.0-abs(p.y)),1.3)*smoothstep(0.0,.08,vUv.x)*(1.0-smoothstep(.7,1.0,vUv.x));col=mix(uTint,vec3(1.0),pow(max(0.0,1.0-abs(p.y)),8.0)*.65);if(uStyle>5.5&&uStyle<6.5)alpha*=.6+.4*sin(vUv.x*100.0-uTime*12.0);}
 else if(uStyle<.5){alpha=pow(max(0.0,1.0-r),2.2);col=mix(uTint,vec3(1.0),pow(max(0.0,1.0-r),8.0)*.6);}
 else if(uStyle<1.5){float n=fbm(vec3(vUv*4.5,vRandom*10.0+uTime*.15));alpha=(1.0-smoothstep(.1,1.0,r))*smoothstep(.15,.8,n);col=mix(uSecondary,uTint,n);}
 else if(uStyle<2.5){float n=fbm(vec3(vUv*4.0+vec2(0.0,-uTime*.7),vRandom*13.0));alpha=smoothstep(.0,.5,(1.0-r)*.8+n*.45-.2);col=mix(uSecondary,uTint,smoothstep(.1,.8,alpha));col=mix(col,vec3(1.0,.85,.45),pow(alpha,5.0));}
 else if(uStyle<3.5){alpha=line(r-.75,.015)+line(r-.67,.008)*.25;col=mix(uTint,vec3(1.0),.2);}
 else if(uStyle<4.5){alpha=pow(max(0.0,1.0-r),1.5);col=mix(uTint,vec3(1.0),.5);}
 else if(uStyle<5.5){float rings=line(r-.92,.006)+line(r-.84,.004)*.5+line(r-.56,.006)+line(r-.22,.006)*.5;float ticks=smoothstep(.95,1.0,cos(a*32.0+uTime*.3))*smoothstep(.82,.86,r)*(1.0-smoothstep(.92,.95,r));float star=line(r-(.35+.12*cos(a*6.0)),.005);alpha=rings+ticks+star*.7;col=mix(uTint,vec3(1.0),.15);}
 else if(uStyle<6.5){alpha=line(p.y+sin(p.x*20.0+uTime*15.0)*.2,.06)*(1.0-smoothstep(.4,1.0,abs(p.x)));col=mix(uTint,vec3(1.0),.5);}
 else{float fresnel=pow(1.0-abs(dot(normalize(vNormal),normalize(vViewDir))),2.5);float grid=pow(max(0.0,sin(vUv.x*100.0)*sin(vUv.y*60.0)),22.0);float scan=line(fract(vWorld.y*.3-uTime*.2)-.5,.025);alpha=.025+fresnel*.8+grid*.08+scan*.15;col=mix(uTint,vec3(1.0),fresnel*.35);}
 vec4 texel=vec4(1.0);if(uHasTexture>.5)texel=texture(uTexture,vUv);
 alpha*=uOpacity*vAlive*texel.a;
 if(uDepthEnabled>.5&&uSoftness>.001){float d=texture(uSceneDepth,gl_FragCoord.xy/uResolution).r;float sceneDistance=uNearFar.x*uNearFar.y/(uNearFar.y-d*(uNearFar.y-uNearFar.x));alpha*=clamp((sceneDistance-vViewDepth)/uSoftness,0.0,1.0);}
 if(alpha<.002)discard;
 outColor=vec4(max(col*uIntensity*texel.rgb,vec3(0.0)),clamp(alpha,0.0,1.0));
}`;
export const vertexPost = /* glsl */ `
precision highp float;out vec2 vUv;
void main(){vUv=uv;gl_Position=vec4(position.xy,0.0,1.0);}
`;
export const fragmentPost = /* glsl */ `
precision highp float;in vec2 vUv;out vec4 outColor;
uniform sampler2D tDiffuse,uSceneColor,uSceneDepth,uEffectMask;
uniform float uTime,uProgress,uStrength;uniform vec3 uTint;uniform vec2 uResolution;
void main(){vec3 color=texture(tDiffuse,vUv).rgb;float ring=texture(uEffectMask,vUv).a;outColor=vec4(color+uTint*ring*uStrength,1.0);}
`;
export const fragmentDistortion = /* glsl */ `
precision highp float;in vec2 vUv;in float vViewDepth;out vec4 outColor;
uniform float uProgress,uStrength,uDepthEnabled;uniform sampler2D uSceneDepth;uniform vec2 uResolution,uNearFar;
void main(){vec2 p=vUv*2.0-1.0;float r=length(p);float ring=exp(-pow((r-mix(.04,.95,uProgress))*18.0,2.0));float mask=ring*(1.0-smoothstep(.9,1.0,r));if(uDepthEnabled>.5){float d=texture(uSceneDepth,gl_FragCoord.xy/uResolution).r;float depth=uNearFar.x*uNearFar.y/(uNearFar.y-d*(uNearFar.y-uNearFar.x));mask*=clamp((depth-vViewDepth)*4.0,0.0,1.0);}outColor=vec4(normalize(p+vec2(.0001))*mask*uStrength*.12,mask*uStrength,mask);}
`;
export const fragmentComposite = /* glsl */ `
precision highp float;in vec2 vUv;out vec4 outColor;
uniform sampler2D tDiffuse,uField;uniform float uChromatic,uFlash,uVignette;uniform vec3 uFlashColor;
void main(){vec2 shift=texture(uField,vUv).rg;vec2 uv=clamp(vUv+shift,vec2(.001),vec2(.999));vec2 c=(uv-.5)*uChromatic;vec3 color=vec3(texture(tDiffuse,uv+c).r,texture(tDiffuse,uv).g,texture(tDiffuse,uv-c).b);color+=uFlashColor*uFlash;float v=dot(vUv-.5,vUv-.5);color*=1.0-uVignette*smoothstep(.05,.65,v);outColor=vec4(color,1.0);}
`;
export function cleanGLSL(source: string): string {
  return source.replace(/^\s*#version[^\n]*\n/gm, '');
}
