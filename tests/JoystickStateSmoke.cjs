const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

// Supply DevEco's TypeScript compiler; no npm dependency is required by the project.
if (!process.argv[2]) throw new Error('Usage: node tests/JoystickStateSmoke.cjs /path/to/typescript.js');
const ts = require(path.resolve(process.argv[2]));
const source = fs.readFileSync(path.join(__dirname,
  '../platform/ohos/template/entry/src/main/ets/components/JoystickState.ets'), 'utf8');
const compiled = ts.transpileModule(source, {
  fileName: 'JoystickState.ts',
  compilerOptions: { target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.CommonJS }
});
const TouchType = { Down: 0, Up: 1, Move: 2, Cancel: 3 };
const context = { exports: {}, TouchType };
vm.runInNewContext(compiled.outputText, context);
const { JoystickState } = context.exports;
const joystick = new JoystickState();
const point = (id, x, y) => ({ id, x, y });
const event = (type, changedTouches, touches = changedTouches) => ({ type, changedTouches, touches });
const near = (actual, expected) => assert.ok(Math.abs(actual - expected) < 1e-8, `${actual} != ${expected}`);
const stopped = () => {
  assert.equal(joystick.active, false);
  near(joystick.offsetX, 0);
  near(joystick.offsetY, 0);
  near(joystick.right, 0);
  near(joystick.forward, 0);
};

stopped();
joystick.handle(event(TouchType.Move, [point(1, 100, 72)]));
stopped();
joystick.handle(event(TouchType.Down, [point(1, 72, 72)]));
assert.equal(joystick.active, true);
joystick.handle(event(TouchType.Move, [point(1, 74, 73)]));
near(joystick.right, 0);
near(joystick.forward, 0);

joystick.handle(event(TouchType.Move, [point(1, 102, 51)]));
near(joystick.right / joystick.forward, 30 / 21);
assert.ok(joystick.right > 0 && joystick.forward > 0, 'Up-right motion was reversed');
assert.notEqual(joystick.right, joystick.forward, 'Direction snapped to a 45-degree key diagonal');

joystick.handle(event(TouchType.Move, [point(1, 372, -328)]));
near(Math.hypot(joystick.offsetX, joystick.offsetY), joystick.travelRadius);
near(joystick.right, 0.6);
near(joystick.forward, 0.8);
assert.ok(Math.abs(joystick.offsetX) + joystick.thumbDiameter / 2 <= joystick.diameter / 2);
assert.ok(Math.abs(joystick.offsetY) + joystick.thumbDiameter / 2 <= joystick.diameter / 2);

joystick.handle(event(TouchType.Down, [point(2, 30, 120)]));
near(joystick.right, 0.6);
joystick.handle(event(TouchType.Up, [point(2, 30, 120)], [point(1, 372, -328)]));
assert.equal(joystick.active, true, 'Releasing another finger stopped movement');
joystick.handle(event(TouchType.Move, [point(2, 10, 10)], [point(2, 10, 10), point(1, 30, 72)]));
near(joystick.right, -1);
near(joystick.forward, 0);
joystick.handle(event(TouchType.Move, [point(1, NaN, 72)]));
near(joystick.right, -1);

joystick.handle(event(TouchType.Up, [point(1, 30, 72)], []));
stopped();
joystick.handle(event(TouchType.Down, [point(3, 72, 114)]));
near(joystick.forward, -1);
joystick.handle(event(TouchType.Cancel, []));
stopped();
joystick.handle(event(TouchType.Down, [point(4, 114, 72)]));
near(joystick.right, 1);
joystick.reset();
stopped();
console.log('Joystick gesture replay passed: 360-degree direction, dead zone, bounds, multi-touch and release/reset.');
