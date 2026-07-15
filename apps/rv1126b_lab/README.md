# RV1126B 触摸软件实验台

这是运行在 ATK-DLRV1126B 开发板上的 ARM64 Qt5 Widgets 程序，不是 Android APK。程序把多个入门实验放在同一个触摸入口中，避免每项实验都要从电脑重复输入命令。

当前包含：

- 主页：显示 CPU、温度、ADC、摄像头、NPU 和网络摘要；
- IO / ADC：实时曲线、十点滑动平均、最小/最大/平均值、触摸阈值、LED 阈值联动和 ADC 调速闪烁；
- 五点触摸：显示最多 5 个同时触点的编号、坐标、轨迹和前两点距离；
- 系统监控：CPU、内存、根分区、温度、运行时间与设备状态；
- 音频实验：触摸录音、实时音量与波形、WAV 保存和触摸回放；
- 摄像头 / AI：检测 IMX415 的 V4L2 格式，并受控启动官方相机或 AiSpark YOLO；
- 实验说明：直接在板上查看演示顺序。

程序只在用户选择 LED 控制模式时接管板载工作灯；切回“只看数据”或退出程序时，会恢复进入前的触发器和亮度。

音频录制使用板载 ES8390。开始录音前程序通过 `alsactl` 保存完整混音状态，只临时调整采集音量和 PGA，不启用 ADC 到扬声器直通；停止录音或退出时恢复原状态。录音保存在 `/userdata/rv1126b_lab/audio/`。

官方相机和 AiSpark 本身是 Qt Quick 独立程序，并且都可能独占 `/dev/video-camera0`。实验台不复制厂商 GPLv3 源码，而是沿用厂商 SystemUI 的进程架构：检查设备后一次只启动一个子程序，子程序退出后回到实验台。

## 构建

首次构建先在 Windows PowerShell 中连接开发板并取得板端 Qt 库：

```powershell
powershell -ExecutionPolicy Bypass -File .\apps\rv1126b_lab\prepare_sysroot.ps1
```

随后在 WSL 中交叉编译：

```powershell
wsl -e sh -lc "cd /mnt/d/WorkSpace/RV1126B && ./apps/rv1126b_lab/build_wsl.sh"
```

输出文件位于被 Git 忽略的 `build/rv1126b-lab/rv1126blab`。

## 安装

```powershell
powershell -ExecutionPolicy Bypass -File .\apps\rv1126b_lab\install.ps1
```

安装位置为 `/opt/ui/src/apps/rv1126blab`，桌面配置追加到 `apk3.cfg`。安装脚本不会自动重启开发板；重启后桌面会出现“RV1126B实验台”。

## 安全冒烟测试

程序支持两个环境变量：

- `RV1126BLAB_READ_ONLY=1`：禁止写入 LED；
- `RV1126BLAB_AUTO_EXIT_MS=2000`：指定时间后自动退出。
- `RV1126BLAB_SCREENSHOT=/tmp/home.png`：按 720×1280 离屏渲染并保存测试截图。
- `RV1126BLAB_START_PAGE=1`：测试时直接打开指定页面（0～6）；
- `RV1126BLAB_AUDIO_TEST_MS=1500`：自动完成一次指定时长的录音测试；
- `RV1126BLAB_AUDIO_DIR=/tmp/audio-test`：测试时改写录音保存目录。
- `RV1126BLAB_VISION_PROBE=1`：自动运行一次只读的 V4L2 格式检测；
- `RV1126BLAB_VISION_LAUNCH_TEST=camera`：自动启动并关闭一次 `camera` 或 `aispark` 子程序。

板端离屏测试示例：

```sh
. /etc/profile
export HOME=/ QT_QPA_PLATFORM=offscreen
export RV1126BLAB_READ_ONLY=1 RV1126BLAB_AUTO_EXIT_MS=2000
/opt/ui/src/apps/rv1126blab
```
