import { common } from '@kit.AbilityKit';
import player from 'libvulkcore.so';
import { VirtualJoystick } from '../components/VirtualJoystick';

@Entry
@Component
struct Index {
  @StorageLink('vulkcoreForeground') private foreground: boolean = true;
  @State private ready: boolean = false;
  @State private error: string = '';
  @State private sceneWidth: number = 0;
  @State private sceneHeight: number = 0;
  private readonly sceneAspectRatio: number = 1920 / 1080;
  private errorTimer: number = -1;

  private updateViewport(width: number, height: number): void {
    if (width <= 0 || height <= 0 || !Number.isFinite(width) || !Number.isFinite(height)) {
      return;
    }
    // Fit the original editor viewport without cropping or independent X/Y scaling.
    this.sceneWidth = Math.min(width, height * this.sceneAspectRatio);
    this.sceneHeight = this.sceneWidth / this.sceneAspectRatio;
  }

  aboutToAppear(): void {
    try {
      const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
      player.prepare(context.resourceManager, context.filesDir);
      this.ready = true;
      this.errorTimer = setInterval(() => {
        const error = player.getError();
        if (error.length > 0) { this.error = error; }
      }, 1000);
    } catch (error) {
      this.error = String(error);
    }
  }

  aboutToDisappear(): void {
    clearInterval(this.errorTimer);
    player.setPaused(true);
  }

  @Builder
  control(label: string, key: number) {
    Button(label)
      .width(58).height(48).backgroundColor('#88344255')
      .onTouch((event: TouchEvent) => {
        if (event.type === TouchType.Down) { player.setKey(key, true); }
        if (event.type === TouchType.Up || event.type === TouchType.Cancel) {
          player.setKey(key, false);
        }
      })
  }

  build() {
    Stack({ alignContent: Alignment.BottomStart }) {
      if (this.ready && this.error.length === 0) {
        Stack({ alignContent: Alignment.Center }) {
          if (this.sceneWidth > 0 && this.sceneHeight > 0) {
            XComponent({ id: 'vulkcore', type: XComponentType.SURFACE, libraryname: 'vulkcore' })
              .width(this.sceneWidth).height(this.sceneHeight)
          }
        }.width('100%').height('100%')
        VirtualJoystick({
          inputEnabled: this.foreground,
          onMove: (right: number, forward: number): void => {
            player.setMovement(right, forward);
          }
        }).margin({ left: 24, bottom: 24 })
        Row({ space: 10 }) {
          this.control('Q', 9)
          this.control('E', 10)
          this.control('R', 11)
          this.control('Jump', 8)
        }.position({ right: 24, bottom: 24 })
        Text('Move: joystick   Look: drag right side   Skills: Q / E / R')
          .fontSize(14).fontColor('#ffffff').position({ left: 24, top: 12 })
          .hitTestBehavior(HitTestMode.None)
      }
      if (this.error.length > 0) {
        Text(this.error).fontSize(16).fontColor('#ffb0a0').padding(24)
          .width('100%').height('100%')
      }
    }.width('100%').height('100%').backgroundColor('#17233a')
      .onSizeChange((_oldSize: SizeOptions, newSize: SizeOptions) => {
        this.updateViewport(Number(newSize.width), Number(newSize.height));
      })
  }
}
