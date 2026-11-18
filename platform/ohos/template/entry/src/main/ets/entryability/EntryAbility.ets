import { UIAbility } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';
import { BusinessError } from '@kit.BasicServicesKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import player from 'libvulkcore.so';

export default class EntryAbility extends UIAbility {
  onWindowStageCreate(stage: window.WindowStage): void {
    this.configureWindow(stage);
  }

  private async configureWindow(stage: window.WindowStage): Promise<void> {
    try {
      const mainWindow = await stage.getMainWindow();
      // Establish the landscape window before XComponent creates its surface.
      await mainWindow.setPreferredOrientation(window.Orientation.AUTO_ROTATION_LANDSCAPE);
      await mainWindow.setWindowLayoutFullScreen(true);
      await mainWindow.setWindowKeepScreenOn(true);
      await stage.loadContent('pages/Index');
    } catch (error) {
      const failure = error as BusinessError;
      hilog.error(0x3200, 'VulkCore', 'Window setup failed: %{public}s', failure.message);
    }
  }

  onForeground(): void {
    AppStorage.setOrCreate('vulkcoreForeground', true);
    player.setPaused(false);
  }
  onBackground(): void {
    AppStorage.setOrCreate('vulkcoreForeground', false);
    player.setPaused(true);
  }
}
