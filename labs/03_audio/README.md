# 实验 03：音频

## 当前已确认

- ALSA 声卡：`rockchip-es8390`
- 录音设备：`hw:0,0`
- 播放设备：`hw:0,0`
- 命令：`arecord`、`aplay`、`amixer` 均存在

当前只完成设备枚举，还没有证明麦克风能录到有效声音。

## 后续最小步骤

1. 用 `amixer` 查看录音通路和增益，不先修改大量控件。
2. 录制 5 秒单声道 WAV。
3. 把 WAV 拉回电脑，确认时长、采样率和文件非全零。
4. 再计算峰值和 RMS，最后做简单波形或音量条显示。

建议的首次录音命令（执行前先确认开发板上的麦克风或音频输入已连接）：

```sh
arecord -D hw:0,0 -f S16_LE -r 16000 -c 1 -d 5 /tmp/mic-test.wav
```

拉回电脑：

```powershell
adb pull /tmp/mic-test.wav .\output\mic-test.wav
```
