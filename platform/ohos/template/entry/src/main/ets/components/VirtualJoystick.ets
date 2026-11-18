import { JoystickState } from './JoystickState';

@Component
export struct VirtualJoystick {
  @Prop @Watch('onInputEnabledChanged') inputEnabled: boolean = true;
  onMove: (right: number, forward: number) => void = (_right: number, _forward: number): void => {};
  @State private thumbX: number = 0;
  @State private thumbY: number = 0;
  @State private dragging: boolean = false;
  private readonly controller: JoystickState = new JoystickState();

  private reset(): void {
    this.controller.reset();
    this.publish();
  }

  private publish(): void {
    this.thumbX = this.controller.offsetX;
    this.thumbY = this.controller.offsetY;
    this.dragging = this.controller.active;
    this.onMove(this.controller.right, this.controller.forward);
  }

  private onInputEnabledChanged(): void {
    if (!this.inputEnabled) {
      this.reset();
    }
  }

  aboutToDisappear(): void {
    this.reset();
  }

  build() {
    Stack({ alignContent: Alignment.Center }) {
      Circle()
        .width(this.controller.diameter).height(this.controller.diameter)
        .fill('#60132232').stroke(this.dragging ? '#cc70cde9' : '#887997aa').strokeWidth(2)
        .hitTestBehavior(HitTestMode.None)
      Circle()
        .width(104).height(104)
        .fill('#221e4055').stroke('#506f9caf').strokeWidth(1)
        .hitTestBehavior(HitTestMode.None)
      Rect().width(72).height(1).fill('#406f9caf').hitTestBehavior(HitTestMode.None)
      Rect().width(1).height(72).fill('#406f9caf').hitTestBehavior(HitTestMode.None)
      Circle()
        .width(this.controller.thumbDiameter).height(this.controller.thumbDiameter)
        .fill(this.dragging ? '#db5cacc8' : '#b34e6b80')
        .stroke(this.dragging ? '#f0c6f1ff' : '#b095b4c5').strokeWidth(2)
        .shadow({ radius: 10, color: '#55000000', offsetX: 0, offsetY: 3 })
        .translate({ x: this.thumbX, y: this.thumbY })
        .hitTestBehavior(HitTestMode.None)
    }
    .width(this.controller.diameter).height(this.controller.diameter)
    .borderRadius(this.controller.diameter / 2)
    .hitTestBehavior(HitTestMode.Block)
    .accessibilityText($r('app.string.movement_joystick'))
    .onTouch((event: TouchEvent) => {
      event.stopPropagation();
      if (this.inputEnabled && this.controller.handle(event)) {
        this.publish();
      }
    })
  }
}
