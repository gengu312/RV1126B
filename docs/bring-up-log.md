# 首次上电检查记录

日期：2026-07-13

## 连接结论

Windows 已识别开发板，设备实例为：

```text
USB\VID_2207&PID_0006\17236123880fc0c5
名称：rk3xxx
驱动：Microsoft WinUSB
状态：Started / OK
```

ADB 输出：

```text
17236123880fc0c5    device transport_id:1
uid=0(root) gid=0(root) groups=0(root)
```

当前无 COM 串口、无开发板 USB 网卡、无 fastboot 设备。开发板 `eth0`、`wlan0`、`wlan1` 当前为 DOWN 且没有 IP。因此近期以 USB ADB 为主，暂不先搭 SSH。

## 板端身份

```text
Alientek RV1126B Board
compatible: rockchip,rv1126b-evb4-v10 / rockchip,rv1126b
Linux ATK-DLRV1126B 6.1.141 aarch64 GNU/Linux
Buildroot 2024.02
RK_BUILD_INFO=... alientek_rv1126b
```

实机约有 2 GB 内存和 32 GB eMMC；根文件系统为 ext4 且当前可写。板端有 `make` 和 `python3`，没有 `gcc/g++/cmake`；电脑端当前也没有 CMake、C/C++ 编译器和 ARM 交叉编译器。因此 Hello World 编译放到“取得厂商工具链”之后，不在此时猜工具链前缀。

板端时钟仍停留在 `1970-01-01`，说明尚未完成 RTC/NTP 校时。采集报告文件名使用电脑时间，因此本次证据仍可排序；后续联网后再单独处理校时，不在本次检查中修改系统时间。

## GPIO

- `/dev/gpiochip0`～`/dev/gpiochip7` 存在。
- `/sys/class/gpio/export` 和 `unexport` 存在。
- `gpiodetect/gpioinfo/gpioget/gpioset` 均未安装。
- debugfs 已能列出驱动占用的 GPIO，例如音频控制、电源、复位和 USB 供电控制线；这些已占用线不能拿来做练习。

厂家核心板引脚表确认板载工作灯为 `WORK_PWM_LED / GPIO0_C5`，实机对应 `/sys/class/leds/work`。已通过 LED class 闪烁 2 次，并确认脚本退出后触发器恢复为 `heartbeat`。外部排针 GPIO 仍需等底板/整板配套 A 盘资料确认后再接线。

## ADC

IIO 设备：

```text
/sys/bus/iio/devices/iio:device0
name=21f10000.saradc
scale=0.219726562
```

首次只读值：

```text
channel0=8191
channel1=615
channel2=8191
channel3=7480
channel4=4223
channel5=63
channel6=572
channel7=8186
```

厂家核心板引脚表和规格书确认，板子右下角的可调电位器连接 `SARADC0_IN4`，对应 `in_voltage4_raw`。实机连续读取 5 次得到 `4216～4224`，约 `926～928 mV`。由 `8191 * scale` 可推算当前驱动的满量程约为 1800 mV；外部扩展针脚的允许电压仍需用完整底板原理图确认。

## 音频

```text
card 0: rockchip-es8390
capture: hw:0,0
playback: hw:0,0
```

`arecord`、`aplay`、`amixer` 均存在，说明后续可以直接做 ALSA 录放实验。尚未录音验证麦克风信号是否正常。

## 屏幕

```text
DRM connector: DSI-1 connected
framebuffer: /dev/fb0 (rockchipdrmfb)
mode: 720x1280
bits_per_pixel: 32
```

结论：显示链路已被内核识别；后续可先显示静态画面，再做 ADC/音量数据界面。

## 摄像头

系统创建了多个 media/video 设备节点，并提供 `media-ctl`、`v4l2-ctl`、GStreamer 和 FFmpeg。但启动日志同时出现：

```text
imx335 ... Unexpected sensor id(000000), ret(-5)
imx415 ... Unexpected sensor id(000000), ret(-5)
imx586 ... Unexpected sensor id(000000), ret(-5)
rkcif ... get remote terminal sensor failed
```

实物已确认配套模组是 `ATK-MCIMX415`。当前系统为两路 IMX415 分别加载驱动，日志中两路都报告 `Unexpected sensor id(000000)`。照片确认模组尚未通过 `2x11` 排针接入底板；灰色 FFC 是备用主机接口，不是镜头到接口板之间的必接线。因此当前直接原因是摄像头没有接入 MIPI CSI，不需要先安装驱动或修改固件。具体安全接法和接好后的验收见 [摄像头实验](../labs/05_camera/README.md)。

## 2026-07-15 复检更新

上述内容是首次上电、摄像头尚未插入时的历史记录。摄像头插好并重启后，已连接的一路识别为 `m01_b_imx415 4-001a`，`/dev/video-camera0` 指向 `/dev/video31`，`rkisp_mainpath` 当前格式为 `3840x2160 NV12`，官方相机可以正常显示画面。

同日确认 Goodix 触摸为 `/dev/input/event1`，坐标 `720x1280`，最多 5 个同时触点；自制 ARM64 Qt5“RV1126B实验台”已安装到桌面并完成 ADC/LED、系统、音频、摄像头/AI、总线和按键的自动冒烟测试。五指触摸与实体按键最终实操仍需在板上手动完成。
