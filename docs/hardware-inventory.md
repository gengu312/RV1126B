# 硬件清单

## 已由系统确认

| 项目 | 当前值 | 证据 |
| --- | --- | --- |
| 开发板 | Alientek RV1126B Board | `/proc/device-tree/model` |
| 设备树兼容项 | `rockchip,rv1126b-evb4-v10` | `/proc/device-tree/compatible` |
| 系统 | Buildroot 2024.02 | `/etc/os-release` |
| 内核/架构 | Linux 6.1.141 / aarch64 | `uname -a` |
| 内存 | 约 2 GB（`MemTotal=2021448 kB`） | `/proc/meminfo` |
| eMMC | 约 32 GB（`mmcblk0=30539776 KiB`） | `/proc/partitions` |
| USB 调试 | ADB，VID:PID `2207:0006` | Windows PnP、`adb devices -l` |
| GPIO | gpiochip0～gpiochip7 | `/dev/gpiochip*`、debugfs |
| ADC | 8 通道 SAR ADC | IIO `iio:device0` |
| 音频 | rockchip-es8390，录音/播放各 1 个 PCM | `/proc/asound/cards`、`arecord -l`、`aplay -l` |
| 显示 | MIPI DSI，720x1280，32 bpp | DRM、`/dev/fb0` |
| 摄像头框架 | `/dev/media0`～`/dev/media6`、多个 `/dev/video*` | 设备节点 |

## 仍需看实物或资料确认

- [ ] 核心板和底板的准确型号、硬件版本号。
- [ ] 屏幕背面型号和触摸控制器型号。
- [ ] 摄像头模组型号、连接的 CSI 口、排线方向。
- [ ] 包装内是否有 USB-TTL 调试串口模块。
- [ ] 扩展排针图、GPIO 电平、ADC 最大输入电压。
- [ ] 板载可安全用于第一个实验的用户 LED、按键或电位器。

建议拍摄并保存到 `output/photos/`（不提交 Git）：

1. 开发板正面全景。
2. 开发板背面全景。
3. 核心板、底板丝印特写。
4. 摄像头正反面和排线连接处。
5. 屏幕背面标签和排线连接处。
6. 现在连接电脑的 USB 接口位置。
