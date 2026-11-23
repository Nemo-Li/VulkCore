import * as THREE from 'three';
export type StageMode = 'dark' | 'light' | 'arena';
export function createStage(scene: THREE.Scene, mode: StageMode = 'dark') {
  const group = new THREE.Group();
  group.name = 'Preview stage';
  group.userData.previewStage = true;
  scene.add(group);
  scene.background = new THREE.Color(
    mode === 'light' ? '#bdc6cc' : mode === 'arena' ? '#202d2c' : '#12161c',
  );
  scene.fog = new THREE.Fog(scene.background, 15, 38);
  const floor = new THREE.Mesh(
    new THREE.PlaneGeometry(60, 60),
    new THREE.MeshStandardMaterial({
      color: mode === 'light' ? '#a6afb5' : mode === 'arena' ? '#38443d' : '#252b34',
      roughness: 0.92,
      metalness: 0.12,
    }),
  );
  floor.rotation.x = -Math.PI / 2;
  group.add(floor);
  const grid = new THREE.GridHelper(
    30,
    60,
    mode === 'light' ? '#8997a0' : '#4a5565',
    mode === 'light' ? '#99a5ac' : '#384352',
  );
  grid.position.y = 0.008;
  grid.userData.editorHelper = true;
  const gm = grid.material as THREE.Material;
  gm.transparent = true;
  gm.opacity = 0.35;
  gm.depthWrite = false;
  group.add(grid);
  const hemi = new THREE.HemisphereLight('#d5e7ff', '#49413b', 2.1);
  group.add(hemi);
  const sun = new THREE.DirectionalLight('#e4e8ff', 2.4);
  sun.position.set(4, 9, 5);
  group.add(sun);
  const reference = new THREE.Group();
  reference.position.set(-2.7, 0, 0);
  reference.visible = false;
  reference.name = '1.8m reference';
  const mat = new THREE.MeshStandardMaterial({ color: '#59636c', roughness: 0.6 });
  const body = new THREE.Mesh(new THREE.CapsuleGeometry(0.18, 0.55, 4, 12), mat);
  body.position.y = 1.08;
  const head = new THREE.Mesh(new THREE.SphereGeometry(0.13, 16, 12), mat);
  head.position.y = 1.65;
  reference.add(body, head);
  for (const sign of [-1, 1]) {
    const leg = new THREE.Mesh(new THREE.CapsuleGeometry(0.075, 0.61, 4, 10), mat);
    leg.position.set(sign * 0.11, 0.38, 0);
    const arm = new THREE.Mesh(new THREE.CapsuleGeometry(0.055, 0.5, 4, 10), mat);
    arm.position.set(sign * 0.29, 1.05, 0);
    arm.rotation.z = sign * 0.15;
    reference.add(leg, arm);
  }
  group.add(reference);
  if (mode === 'arena') {
    for (const [x, z, h] of [
      [3, 1, 1.8],
      [-3, 2, 2.5],
      [1, -4, 1.2],
    ]) {
      const box = new THREE.Mesh(
        new THREE.BoxGeometry(1, h, 1),
        new THREE.MeshStandardMaterial({ color: '#526057', roughness: 0.9 }),
      );
      box.position.set(x, h / 2, z);
      group.add(box);
    }
  }
  return {
    group,
    grid,
    reference,
    dispose() {
      group.traverse((o) => {
        if (o instanceof THREE.Mesh || o instanceof THREE.LineSegments) {
          o.geometry.dispose();
          for (const m of Array.isArray(o.material) ? o.material : [o.material]) m.dispose();
        }
      });
      group.removeFromParent();
    },
  };
}
