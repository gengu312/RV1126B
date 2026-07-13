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

结论：GPIO 软件接口存在，但必须先从原理图确认一个排针上的空闲 GPIO，当前不执行输出切换。

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

这些是当前接线状态下的瞬时值，不代表某个扩展针脚已经确认。由 `8191 * scale` 可推算当前驱动的满量程约为 1800 mV，但实际外接电压限制仍需用本板原理图/手册确认。

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

结论：媒体框架和驱动存在，但传感器没有被正确探测。常见方向是摄像头未接好、排线方向/接口错误、实际模组型号与当前设备树不一致；未检查实物前不下最终结论，也不进行固件修改。
