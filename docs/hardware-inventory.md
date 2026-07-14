# 硬件清单

## 已由系统确认

| 项目 | 当前值 | 证据 |
| --- | --- | --- |
| 整套开发板 | `ATK-DLRV1126B`；核心板安装在配套底板上 | `hostname`、厂商规格书 |
| 核心板 | `ATK-CLRV1126B` | 厂商规格书、核心板资料包 |
| 设备树型号 | Alientek RV1126B Board | `/proc/device-tree/model` |
| 设备树兼容项 | `rockchip,rv1126b-evb4-v10` | `/proc/device-tree/compatible` |
| 系统 | Buildroot 2024.02 | `/etc/os-release` |
| 内核/架构 | Linux 6.1.141 / aarch64 | `uname -a` |
| 内存 | 约 2 GB（`MemTotal=2021448 kB`） | `/proc/meminfo` |
| eMMC | 约 32 GB（`mmcblk0=30539776 KiB`） | `/proc/partitions` |
| USB 调试 | ADB，VID:PID `2207:0006` | Windows PnP、`adb devices -l` |
| GPIO | gpiochip0～gpiochip7 | `/dev/gpiochip*`、debugfs |
| 板载工作灯 | `WORK_PWM_LED / GPIO0_C5`，LED class `work` | 核心板引脚表 B 座 53、实机 sysfs |
| ADC | 8 通道 SAR ADC | IIO `iio:device0` |
| 板载可调电位器 | `SARADC0_IN4 / in_voltage4_raw` | 核心板引脚表 A 座 70、核心板规格书、实机 IIO |
| 音频 | rockchip-es8390，录音/播放各 1 个 PCM | `/proc/asound/cards`、`arecord -l`、`aplay -l` |
| 显示 | MIPI DSI，720x1280，32 bpp | DRM、`/dev/fb0` |
| 屏幕实物 | `ALIENTEK 5.5" MIPI LCD` | 屏幕正面丝印、实物照片 |
| 摄像头模组 | `ATK-MCIMX415`，4-Lane MIPI，800 万像素，最高 3840x2160 | 模组丝印、厂家模块页 |
| 底板可见资源 | 40PIN 扩展座、按键、ADC 电位器、网口、USB、电源和工业接口 | 实物照片 |
| 摄像头框架 | `/dev/media0`～`/dev/media6`、多个 `/dev/video*` | 设备节点 |

## 实物照片结论

[2026-07-14 开发板正面照片](../output/photos/2026-07-14-board-front.jpg) 已保存到 Git 忽略的 `output/photos/`。

摄像头的[正面照片](../output/photos/2026-07-14-camera-module-front.jpg)和[背面照片](../output/photos/2026-07-14-camera-module-back.jpg)也已保存。照片确认模组由镜头/传感器小板、短黑色内部软连接、`ATK-MCIMX415` 接口板和 `2x11` 公排针组成。用户后来插入的灰色 FFC 位于备用主机接口，本开发板不使用；自由端不能接到底板，也不能带着裸露触点碰到电路后通电。

照片确认当前硬件是“大底板 + 核心板 + 5.5 英寸 MIPI 屏幕”的完整开发套件，不是单独裸核心板。核心板位于屏幕覆盖区域下方，当前照片不能看清核心板和底板的 PCB 版本丝印；如需确认版本，应在断电后补拍屏幕下方、板子背面或侧面丝印，不要带电拆装。

## 仍需看实物或资料确认

- [ ] 核心板和底板的 PCB 硬件版本号/丝印（型号已确认）。
- [ ] 屏幕背面型号和触摸控制器型号。
- [x] 摄像头模组型号：`ATK-MCIMX415`。
- [x] 灰色 FFC 用途：备用主机接口，DLRV1126B 使用 `2x11` 排针时不接。
- [ ] 灰色 FFC 背面座的锁扣形式（取下前需补拍特写，不能硬拽）。
- [ ] 最终连接的 MIPI CSI 口及其丝印。
- [ ] 包装内是否有 USB-TTL 调试串口模块。
- [ ] 扩展排针图、GPIO 电平、ADC 最大输入电压。
- [ ] 板载按键的网络名和 Linux 输入事件映射。

建议拍摄并保存到 `output/photos/`（不提交 Git）：

1. 开发板正面全景。
2. 开发板背面全景。
3. 核心板、底板丝印特写。
4. 摄像头正反面和排线连接处。
5. 屏幕背面标签和排线连接处。
6. 现在连接电脑的 USB 接口位置。
