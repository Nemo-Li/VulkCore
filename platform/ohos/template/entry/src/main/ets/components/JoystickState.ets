export class JoystickState {
  readonly diameter: number;
  readonly thumbDiameter: number;
  readonly travelRadius: number;
  private readonly deadZoneRatio: number;
  private fingerId: number = -1;
  offsetX: number = 0;
  offsetY: number = 0;
  right: number = 0;
  forward: number = 0;

  constructor(diameter: number = 144, thumbDiameter: number = 60, deadZoneRatio: number = 0.12) {
    this.diameter = diameter;
    this.thumbDiameter = thumbDiameter;
    this.travelRadius = (diameter - thumbDiameter) / 2;
    this.deadZoneRatio = deadZoneRatio;
  }

  get active(): boolean {
    return this.fingerId >= 0;
  }

  reset(): void {
    this.fingerId = -1;
    this.offsetX = 0;
    this.offsetY = 0;
    this.right = 0;
    this.forward = 0;
  }

  handle(event: TouchEvent): boolean {
    if (event.type === TouchType.Cancel) {
      this.reset();
      return true;
    }
    if (event.type === TouchType.Up) {
      if (event.changedTouches.some((point: TouchObject): boolean => point.id === this.fingerId)) {
        this.reset();
        return true;
      }
      return false;
    }
    if (event.type === TouchType.Down) {
      if (this.active || event.changedTouches.length === 0) {
        return false;
      }
      const point = event.changedTouches[0];
      if (point.id < 0 || !Number.isFinite(point.x) || !Number.isFinite(point.y)) {
        return false;
      }
      this.fingerId = point.id;
      return this.updatePosition(point);
    }
    if (event.type === TouchType.Move && this.active) {
      const point = event.changedTouches.find((touch: TouchObject): boolean => touch.id === this.fingerId) ??
        event.touches.find((touch: TouchObject): boolean => touch.id === this.fingerId);
      if (point !== undefined) {
        return this.updatePosition(point);
      }
    }
    return false;
  }

  private updatePosition(point: TouchObject): boolean {
    if (!Number.isFinite(point.x) || !Number.isFinite(point.y)) {
      return false;
    }
    const dx = point.x - this.diameter / 2;
    const dy = point.y - this.diameter / 2;
    const distance = Math.hypot(dx, dy);
    const scale = distance > this.travelRadius ? this.travelRadius / distance : 1;
    this.offsetX = dx * scale;
    this.offsetY = dy * scale;
    const outsideDeadZone = distance > this.travelRadius * this.deadZoneRatio;
    this.right = outsideDeadZone ? this.offsetX / this.travelRadius : 0;
    // ArkUI's Y axis points down; the player's forward axis points up the pad.
    this.forward = outsideDeadZone ? -this.offsetY / this.travelRadius : 0;
    return true;
  }
}
