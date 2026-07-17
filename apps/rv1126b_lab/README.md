# RV1126B 触摸软件实验台

这是运行在 ATK-DLRV1126B 开发板上的 ARM64 Qt5 Widgets 程序，不是 Android APK。程序把多个入门实验放在同一个触摸入口中，避免每项实验都要从电脑重复输入命令。

当前包含：

- 主页：显示 CPU、温度、ADC、摄像头、NPU 和网络摘要；
- IO / ADC：实时曲线、十点滑动平均、最小/最大/平均值、触摸阈值、LED 阈值联动和 ADC 调速闪烁；
- 五点触摸：显示最多 5 个同时触点的编号、坐标、轨迹和前两点距离，刷新限制在约 30 FPS；
- 系统监控：CPU、内存、根分区、温度、运行时间与设备状态；
- 音频实验：触摸录音、实时音量与波形、WAV 保存和触摸回放；
- 摄像头 / AI：检测 IMX415 的 V4L2 格式，并受控启动官方相机或 AiSpark YOLO；
- 总线 / 按键：只读列出 I2C、UART、CAN、PWM、SPI 状态，并实时显示板载实体按键事件；
- 一键自检：汇总 IO/ADC、触摸、音频、摄像头/NPU、网络无线、扩展总线和系统健康状态，并保存纯文本报告；
- 使用说明：直接在板上查看各功能的操作步骤。

主页集中显示状态摘要和功能入口。各功能页底部固定提供“实验台主页”和“返回系统桌面”两个按钮，实体返回键也会统一返回主页；较长页面支持手指拖动滚动，不会再把返回入口挤出屏幕。

程序只在用户选择 LED 控制模式时接管板载工作灯；切回“只看数据”或退出程序时，会恢复进入前的触发器和亮度。

音频录制使用板载 ES8390。开始录音前程序异步保存完整混音状态，只临时调整采集音量和 PGA，不启用 ADC 到扬声器直通；每条混音命令都有超时保护，停止录音或离开页面后异步恢复原状态，不会再堵塞主页与退出按钮。录音保存在 `/userdata/rv1126b_lab/audio/`。

自检报告默认保存在 `/userdata/rv1126b_lab/reports/`。如果板卡尚未联网校时，文件名会改用开机时长，报告中也会明确标注“系统时钟未校准”，不会把错误日期当成真实测试时间。

官方相机和 AiSpark 本身是 Qt Quick 独立程序，并且都可能独占 `/dev/video-camera0`。实验台不复制厂商 GPLv3 源码，而是沿用厂商 SystemUI 的进程架构：检查设备后一次只启动一个子程序；实验台入口最长运行 15 秒，子程序未主动退出时会终止并自动返回，避免停留在无法返回的全屏页面。摄像头格式检测也有 5 秒超时保护。

## 构建

首次构建先在 Windows PowerShell 中连接开发板并取得板端 Qt 库：

```powershell
powershell -ExecutionPolicy Bypass -File .\apps\rv1126b_lab\prepare_sysroot.ps1
```

随后在 WSL 中交叉编译：

```powershell
wsl -e sh -lc "cd /mnt/d/WorkSpace/RV1126B && ./apps/rv1126b_lab/build_wsl.sh"
```

构建脚本关闭本程序未使用的 C++ 异常展开，避免较新的 WSL GCC 引入板端 `libstdc++` 不支持的 `CXXABI_1.3.15`。

输出文件位于被 Git 忽略的 `build/rv1126b-lab/rv1126blab`。

## 安装

```powershell
powershell -ExecutionPolicy Bypass -File .\apps\rv1126b_lab\install.ps1
```

安装位置为 `/opt/ui/src/apps/rv1126blab`。当前桌面中“ADC 调速灯”位于 `apk1.cfg` 第 13 项，安装器会把“RV1126B实验台”放在紧邻的第 14 项；屏幕上两者位于同一页相邻空位。安装器会从 `apk2.cfg` 和底部 Dock 的 `apk3.cfg` 中移除旧的实验台入口，不改变这些页面其余图标的顺序。安装脚本不会自动重启开发板，重启后桌面会刷新入口位置。

安装器会先把新文件放在 `/tmp` 并检查板端动态库兼容性；依赖缺失时会停止安装，不覆盖当前可运行版本。

程序顶部和底部都提供返回入口。由于厂商的控制中心/动态岛可能覆盖屏幕顶部触摸区，底部红色“返回系统桌面”是主要退出入口；点击后界面会立即隐藏并显示 SystemUI 桌面，程序在后台停止录音、恢复混音状态后退出，异常时由总超时保护结束清理。底部蓝色“实验台主页”只返回本程序主页，不会退出。

## 已完成的实机验证

2026-07-16 已在当前开发板完成：

- ARM64 交叉编译、`ldd` 动态库检查、离屏与 Wayland 启动均通过；桌面配置重复安装后仍只有一条；
- 主页和 8 个功能页按 `720x1280` 完成布局回归检查，无全局导航按钮越界；较长页面由滚动区域保护；
- ADC 通道 4 能连续刷新，系统页能读取 CPU、内存、存储、温度、摄像头和 NPU；
- 一键自检能识别当前 7 类硬件状态，保存报告时不会改动 LED 或 ALSA 状态；当前板卡结果为 7 类均可用，其中网络未连接、SPI 未启用和系统时钟未校准作为提示信息；
- 自动录音生成约 1.3 秒的 `16000 Hz / 单声道 / 16 bit PCM` WAV，测试前后完整 ALSA 状态一致；
- V4L2 检测得到 IMX415 主通道 `3840x2160 NV12`，官方相机和 AiSpark 均能由实验台启动、关闭且无残留进程；
- 只读检测到 6 路 I2C、2 个 `ttyS` 串口节点、1 个 `ttyFIQ` 调试节点、2 路 CAN、3 个 PWM，当前无 `spidev`；
- 真实 `adc-keys` 节点能打开；另用标准 Linux `input_event` 验证了按下、松开、键名和计数逻辑；
- 60 秒后台稳定性测试中，常驻内存约从 `34156 KB` 到 `34188 KB`，文件句柄保持 5，温度约 `38.8～39.0°C`，到时正常退出且无残留进程。

仍需人在屏幕前完成的验收是：实际五指同时触摸、逐个按下实体按键、对着麦克风说话并听回放。这些动作不能只靠 ADB 代替。

## 安全冒烟测试

程序支持以下测试环境变量：

- `RV1126BLAB_READ_ONLY=1`：禁止写入 LED；
- `RV1126BLAB_AUTO_EXIT_MS=2000`：指定时间后自动退出；与结果型自动测试同时使用时作为失败看门狗，超时返回非零退出码；
- `RV1126BLAB_SCREENSHOT=/tmp/home.png`：按 720×1280 离屏渲染并保存测试截图；
- `RV1126BLAB_START_PAGE=1`：测试时直接打开指定页面（0～8）；
- `RV1126BLAB_AUDIO_TEST_MS=1500`：自动完成一次指定时长的录音测试；
- `RV1126BLAB_AUDIO_DIR=/tmp/audio-test`：测试时改写录音保存目录；
- `RV1126BLAB_MIXER_TIMEOUT_MS=1000`：测试时改写单条混音命令超时；
- `RV1126BLAB_ALSACTL=/tmp/fake-alsactl`：测试时替换 `alsactl` 路径；
- `RV1126BLAB_AMIXER=/tmp/fake-amixer`：测试时替换 `amixer` 路径；
- `RV1126BLAB_AUDIO_NO_MIXER=1`：仅在手工隔离检查中跳过混音命令，不能与自动音频测试同时使用；
- `RV1126BLAB_VISION_PROBE=1`：自动运行一次只读的 V4L2 格式检测；
- `RV1126BLAB_VISION_LAUNCH_TEST=camera`：自动启动并关闭一次 `camera` 或 `aispark` 子程序；
- `RV1126BLAB_EXTERNAL_TIMEOUT_MS=1500`：测试时改写相机/AI 自动返回时限；
- `RV1126BLAB_HARDWARE_TAB=key`：测试时直接打开板载物理按键子页；
- `RV1126BLAB_EXIT_TEST=1`：模拟点击“返回桌面”按钮并验证进程退出；
- `RV1126BLAB_SELF_TEST_SAVE=1`：自动执行一键自检并保存报告；
- `RV1126BLAB_REPORT_DIR=/tmp/selftest`：测试时改写自检报告目录；
- `RV1126BLAB_LAYOUT_TEST=1`：逐页检查顶部、内容区和底部导航是否在窗口内；
- `RV1126BLAB_HOME_TEST=1`：验证指定功能页能通过底部按钮回到主页，必须同时设置 `START_PAGE=1..8`；
- `RV1126BLAB_BACK_TEST=1`：验证指定功能页能通过返回键回到主页，必须同时设置 `START_PAGE=1..8`。

`LAYOUT_TEST`、`HOME_TEST` 和 `BACK_TEST` 一次只能启用一个，避免多个退出型测试互相掩盖结果。

板端离屏测试示例：

```sh
. /etc/profile
export HOME=/ QT_QPA_PLATFORM=offscreen
export RV1126BLAB_READ_ONLY=1 RV1126BLAB_AUTO_EXIT_MS=2000
/opt/ui/src/apps/rv1126blab
```
