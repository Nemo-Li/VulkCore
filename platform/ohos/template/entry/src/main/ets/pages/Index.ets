import { common } from '@kit.AbilityKit';
import player from 'libvulkcore.so';
import { LoadingScreen } from '../components/LoadingScreen';
import { VirtualJoystick } from '../components/VirtualJoystick';

// These progress boundaries match the native getLoadingProgress contract.
enum LoadingMilestone {
  AssetsReady = 75,
  LoadingScene = 88,
  WaitingForFrame = 97,
  FirstFramePresented = 100
}

@Entry
@Component
struct Index {
  @StorageLink('vulkcoreForeground') private foreground: boolean = true;
  @State private progress: number = 0;
  @State private error: string = '';
  private statusTimer: number = -1;

  aboutToAppear(): void {
    this.progress = 0;
    this.error = '';
    try {
      const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
      player.setPaused(!this.foreground);
      player.prepare(context.resourceManager, context.filesDir);
      this.updateLoadingStatus();
    } catch (error) {
      this.error = String(error);
    }
  }

  aboutToDisappear(): void {
    clearTimeout(this.statusTimer);
    player.setPaused(true);
  }

  private updateLoadingStatus(): void {
    const error = player.getError();
    if (error.length > 0) {
      this.error = error;
      return;
    }
    this.progress = player.getLoadingProgress();
    const delay = this.progress < LoadingMilestone.FirstFramePresented ? 80 : 1000;
    this.statusTimer = setTimeout(() => this.updateLoadingStatus(), delay);
  }

  private loadingStage(): Resource {
    if (this.progress < LoadingMilestone.AssetsReady) {
      return $r('app.string.loading_resources');
    }
    if (this.progress < LoadingMilestone.LoadingScene) {
      return $r('app.string.loading_engine');
    }
    if (this.progress < LoadingMilestone.WaitingForFrame) {
      return $r('app.string.loading_scene');
    }
    return $r('app.string.loading_first_frame');
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
      if (this.progress >= LoadingMilestone.AssetsReady && this.error.length === 0) {
        // The scene projection performs uniform cover/crop at screen resolution.
        XComponent({ id: 'vulkcore', type: XComponentType.SURFACE, libraryname: 'vulkcore' })
          .width('100%').height('100%')
      }
      if (this.progress >= LoadingMilestone.FirstFramePresented && this.error.length === 0) {
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
      if (this.progress < LoadingMilestone.FirstFramePresented || this.error.length > 0) {
        LoadingScreen({
          progress: this.progress,
          stage: this.loadingStage(),
          error: this.error
        }).zIndex(1)
      }
    }.width('100%').height('100%').backgroundColor($r('app.color.start_window_background'))
      .expandSafeArea([SafeAreaType.SYSTEM, SafeAreaType.CUTOUT], [
        SafeAreaEdge.TOP, SafeAreaEdge.BOTTOM, SafeAreaEdge.START, SafeAreaEdge.END
      ])
  }
}
