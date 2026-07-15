# RV1126B 入门实验仓库

这是正点原子 `ATK-DLRV1126B` 开发板的学习记录。近期目标按导师要求调整为：

```text
连接并识别开发板 -> GPIO -> ADC -> 音频 -> 屏幕 -> 摄像头/NPU
```

你提到的“ATC 数据”当前暂按 **ADC（模数转换）数据** 理解；如果导师指的是其他模块，需要再修正。

## 当前已确认状态（2026-07-15）

- Windows 已识别 USB 设备 `2207:0006 rk3xxx`，ADB 在线。
- 可直接执行 `adb shell`，进入后为开发板 `root` 用户。
- 板卡自报 `Alientek RV1126B Board`，系统为 64 位 `Buildroot 2024.02`，内核为 `Linux 6.1.141`。
- 当前 USB 连接只有 ADB，不会出现 COM 口；开发板网口和 Wi-Fi 当前也没有 IP。
- GPIO 控制器和旧 sysfs GPIO 接口存在，但系统没有安装 `gpiodetect/gpioinfo/gpioget/gpioset`。
- SAR ADC 已由 Linux IIO 驱动识别，可读取 8 个通道；当前驱动输出 `scale=0.219726562`。
- 音频录放设备已识别，ALSA 卡为 `rockchip-es8390`。
- MIPI DSI 屏幕已连接：`720x1280`、32 bpp、`/dev/fb0`。
- Goodix 触摸屏为 `/dev/input/event1`，坐标范围 `720x1280`，驱动提供 5 个同时触点。
- 配套摄像头已确认是 `ATK-MCIMX415`；灰色 FFC 是备用主机接口，本板使用模组末端的 `2x11` 排针直插 MIPI CSI。
- 已连接的 IMX415 能正常出图：`/dev/video-camera0 -> /dev/video31`，主通道为 `3840x2160 NV12`；另一条空闲 CSI 仍可能留下探测失败日志。
- “RV1126B实验台”已安装到开发板桌面，集成 ADC/LED、五点触摸、系统、音频、摄像头/AI、总线和按键实验。

完整证据见 [docs/bring-up-log.md](docs/bring-up-log.md)。

## 触摸软件实验台

开发板桌面的“RV1126B实验台”是本项目自行交叉编译并安装的 ARM64 Qt5 程序，不是 Android APK。进入后可直接触摸完成：

- ADC 曲线、滤波、统计、阈值联动和 LED 调速；
- 五点触摸轨迹、坐标和距离；
- CPU、内存、存储、温度与设备状态；
- ES8390 录音、实时音量/波形、WAV 保存和回放；
- IMX415 格式检测，以及受控启动官方相机和 AiSpark；
- I2C、UART、CAN、PWM、SPI 只读诊断和板载按键计数。

源代码、构建、安装和安全测试方法见 [apps/rv1126b_lab/README.md](apps/rv1126b_lab/README.md)。原来的 [ADC 调速灯](apps/adc_led_touch/README.md) 仍保留为单功能入门原型。

## 现在先做什么

### 1. 确认电脑还能看到开发板

在本目录打开 PowerShell：

```powershell
adb devices
adb shell
```

看到一行设备序列号并进入 `ATK-DLRV1126B` 的命令行，就说明连接正常。退出板端命令行使用 `exit`。

### 2. 一键保存板卡信息

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\collect_board_info.ps1
```

报告会保存在 `output/board-info-时间.txt`，该目录已被 Git 忽略。

### 3. 读取当前 ADC 数据

```powershell
adb push .\labs\02_adc\read_adc.sh /tmp/rv1126b-read-adc.sh
adb shell sh /tmp/rv1126b-read-adc.sh
```

这一步只读取现有值，不需要外接电压。接线前必须先完成 [docs/pin-map.md](docs/pin-map.md)，确认物理针脚、量程和电平；**不要把 USB 的 5V 直接接到 ADC 或 GPIO**。

### 4. 用板载工作灯完成第一个 GPIO 输出实验

厂家引脚表和实机已确认板载工作灯由 `/sys/class/leds/work` 管理，不需要外接线：

```powershell
adb push .\labs\01_gpio\blink_work_led.sh /tmp/rv1126b-blink-led.sh
adb shell sh /tmp/rv1126b-blink-led.sh
```

脚本会闪烁 3 次并恢复原来的 `heartbeat`。底板原理图现已到齐，但外部排针 GPIO 仍须先在 [docs/pin-map.md](docs/pin-map.md) 中核对物理针脚、复用和电平后再接线，具体说明见 [labs/01_gpio/README.md](labs/01_gpio/README.md)。

## 学习顺序和验收

1. 连接：`adb shell` 每次都能稳定登录。
2. 板卡信息：采集报告中能看到型号、系统、GPIO、IIO、ALSA、DRM 和摄像头状态。
3. GPIO 输出：只使用已经从原理图确认的空闲引脚，完成 LED 亮灭。
4. GPIO 输入：读取按键 0/1，并理解上拉、下拉和防抖。
5. ADC：安全输入三个已知电平，原始值随电压单调变化。
6. 组合：ADC 超过阈值时切换 GPIO，连续运行 10 分钟。
7. 音频：录制 5 秒 WAV，确认不是全零或静音。
8. 屏幕：先显示静态文字，再显示 ADC 和音量数值。

各阶段记录入口：

- [硬件清单](docs/hardware-inventory.md)
- [引脚核对表](docs/pin-map.md)
- [GPIO 实验](labs/01_gpio/README.md)
- [ADC 实验](labs/02_adc/README.md)
- [音频实验](labs/03_audio/README.md)
- [屏幕实验](labs/04_display/README.md)
- [摄像头安装与验收](labs/05_camera/README.md)
- [官方资料入口](docs/resources.md)
- [本地厂家资料盘点与磁盘策略](docs/vendor-materials.md)
- [并行处理与无线并发初步测试](docs/parallel-processing-test.md)

## 目前不要做

- 不烧录固件、不改 U-Boot、内核和设备树。
- 不直接套用老 RV1126 的 SDK、镜像、GPIO 编号或二进制库。
- 不使用 `devmem` 直接改寄存器作为入门实验。
- 不在未确认电平和量程前外接 5V 或未知模拟信号。
- 不急着做 RKNN/NPU；先把 GPIO、ADC、音频、显示链路跑稳。

## 厂家资料存放位置

厂家资料存放在本项目的 `vendor-resources/alientek/` 目录，并被 Git 忽略。开发板 A 盘、开发板 B 盘和核心板 A 盘均已下载完整；B 盘的大型虚拟机和 SDK 暂时保持压缩状态，避免占满磁盘。目录、用途和缺口见 [docs/vendor-materials.md](docs/vendor-materials.md)。
