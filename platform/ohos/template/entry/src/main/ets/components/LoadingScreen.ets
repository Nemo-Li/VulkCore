@Component
export struct LoadingScreen {
  @Prop progress: number = 0;
  @Prop stage: Resource = $r('app.string.loading_resources');
  @Prop error: string = '';

  build() {
    Stack({ alignContent: Alignment.Bottom }) {
      Image($r('app.media.game_loading'))
        .width('100%').height('100%')
        .objectFit(ImageFit.Cover)
        .draggable(false)
        .accessibilityLevel('no')

      Column()
        .width('100%').height('100%')
        .linearGradient({
          direction: GradientDirection.Bottom,
          colors: [
            [$r('app.color.loading_scrim_top'), 0],
            [$r('app.color.loading_scrim_middle'), 0.48],
            [$r('app.color.loading_scrim_bottom'), 1]
          ]
        })

      Text($r('app.string.app_name'))
        .position({ left: '8%', top: '16%' })
        .constraintSize({ maxWidth: '50%' })
        .fontSize(42)
        .fontWeight(FontWeight.Bold)
        .fontColor($r('app.color.loading_text'))
        .maxLines(2)
        .textOverflow({ overflow: TextOverflow.Ellipsis })
        .shadow({ radius: 16, color: $r('app.color.loading_scrim_bottom'), offsetY: 3 })

      Column({ space: 10 }) {
        if (this.error.length > 0) {
          Text($r('app.string.loading_failed'))
            .fontSize(16)
            .fontWeight(FontWeight.Bold)
            .fontColor($r('app.color.loading_error'))
          Text(this.error)
            .fontSize(13)
            .fontColor($r('app.color.loading_text'))
            .maxLines(3)
            .textOverflow({ overflow: TextOverflow.Ellipsis })
            .copyOption(CopyOptions.LocalDevice)
        } else {
          Row() {
            Text(this.stage)
              .fontSize(14)
              .fontColor($r('app.color.loading_text'))
            Blank()
            Text(this.progress.toString() + '%')
              .fontSize(14)
              .fontWeight(FontWeight.Bold)
              .fontColor($r('app.color.loading_accent'))
          }.width('100%')

          Progress({ value: this.progress, total: 100, type: ProgressType.Linear })
            .width('100%')
            .color($r('app.color.loading_accent'))
            .backgroundColor($r('app.color.loading_track'))
            .style({ strokeWidth: 5, enableSmoothEffect: true })
            .accessibilityText(this.stage)
        }
      }
      .width('70%')
      .constraintSize({ maxWidth: 560 })
      .margin({ bottom: 34 })
      .alignItems(HorizontalAlign.Start)
    }
    .width('100%').height('100%')
    .backgroundColor($r('app.color.start_window_background'))
    .hitTestBehavior(HitTestMode.Block)
  }
}
