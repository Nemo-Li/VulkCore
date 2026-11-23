import * as THREE from 'three';
import { hashString, random, type Layer } from '../core/model';
export type ParticleData = {
  positions: Float32Array;
  velocities: Float32Array;
  starts: Float32Array;
  lifetimes: Float32Array;
  sizes: Float32Array;
  rotations: Float32Array;
  randoms: Float32Array;
};
export function generateParticles(layer: Layer, seed: number): ParticleData {
  const e = layer.emitter,
    n = e.count,
    s = seed ^ hashString(layer.id);
  const data: ParticleData = {
    positions: new Float32Array(n * 3),
    velocities: new Float32Array(n * 3),
    starts: new Float32Array(n),
    lifetimes: new Float32Array(n),
    sizes: new Float32Array(n),
    rotations: new Float32Array(n),
    randoms: new Float32Array(n),
  };
  for (let i = 0; i < n; i++) {
    const r = (k: number) => random(s, i * 17 + k);
    const theta = r(0) * Math.PI * 2;
    let y = r(1) * 2 - 1,
      radial = Math.sqrt(Math.max(0, 1 - y * y));
    let x = Math.cos(theta) * radial,
      z = Math.sin(theta) * radial;
    if (e.shape === 'cone') {
      y = 1 - r(1) * (1 - Math.cos((e.angle * Math.PI) / 180));
      radial = Math.sqrt(1 - y * y);
      x = Math.cos(theta) * radial;
      z = Math.sin(theta) * radial;
    }
    const radius = e.radius * Math.cbrt(r(2));
    if (e.shape === 'box') {
      data.positions.set(
        [(r(3) * 2 - 1) * e.radius, (r(4) * 2 - 1) * e.radius, (r(5) * 2 - 1) * e.radius],
        i * 3,
      );
    } else if (e.shape === 'ring' || e.shape === 'cone') {
      data.positions.set(
        [
          Math.cos(theta) * e.radius * (e.shape === 'cone' ? Math.sqrt(r(2)) : 1),
          0,
          Math.sin(theta) * e.radius * (e.shape === 'cone' ? Math.sqrt(r(2)) : 1),
        ],
        i * 3,
      );
    } else if (e.shape === 'sphere')
      data.positions.set([x * radius, y * radius, z * radius], i * 3);
    if (e.shape === 'ring') {
      x = Math.cos(theta);
      y = (r(3) - 0.5) * 0.2;
      z = Math.sin(theta);
    }
    const speed = 1 - e.speedRandom * r(6);
    data.velocities.set([x * speed, y * speed, z * speed], i * 3);
    data.starts[i] = e.mode === 'continuous' ? i / e.rate : r(7) * e.emissionWindow;
    data.lifetimes[i] = e.lifetime * (1 - e.lifetimeRandom * r(8));
    data.sizes[i] = 1 - e.sizeRandom * r(9);
    data.rotations[i] = r(10) * Math.PI * 2;
    data.randoms[i] = r(11);
  }
  return data;
}
export function particleState(data: ParticleData, i: number, layer: Layer, time: number) {
  const e = layer.emitter;
  let birth = data.starts[i];
  const raw = time - birth;
  if (e.mode === 'continuous' && raw >= 0)
    birth += Math.floor(raw / (e.count / e.rate)) * (e.count / e.rate);
  const age = time - birth,
    alive = age >= 0 && age <= data.lifetimes[i] && birth <= e.emissionWindow;
  const t = Math.max(0, age),
    travel = e.drag > 0.001 ? (1 - Math.exp(-e.drag * t)) / e.drag : t;
  const p = [0, 1, 2].map(
    (axis) =>
      data.positions[i * 3 + axis] +
      data.velocities[i * 3 + axis] * e.speed * travel +
      (axis === 1 ? e.gravity * t * t * 0.5 : 0) +
      Math.sin([1.7, 2.1, 1.3][axis] * t + data.randoms[i] * 25 + [0, 2, 4][axis]) *
        e.turbulence *
        t,
  );
  return {
    alive,
    birth,
    age,
    life: Math.max(0, Math.min(1, age / data.lifetimes[i])),
    position: p,
  };
}
export function particleGeometry(data: ParticleData): THREE.InstancedBufferGeometry {
  const plane = new THREE.PlaneGeometry(1, 1);
  const g = new THREE.InstancedBufferGeometry();
  g.index = plane.index;
  g.attributes = plane.attributes;
  g.instanceCount = data.starts.length;
  const attrs: Record<string, [Float32Array, number]> = {
    aPosition: [data.positions, 3],
    aVelocity: [data.velocities, 3],
    aStart: [data.starts, 1],
    aLifetime: [data.lifetimes, 1],
    aSize: [data.sizes, 1],
    aRotation: [data.rotations, 1],
    aRandom: [data.randoms, 1],
  };
  for (const [name, [array, size]] of Object.entries(attrs))
    g.setAttribute(name, new THREE.InstancedBufferAttribute(array, size));
  return g;
}
