# 官方资料入口

## 与当前整板对应的资料

- [正点原子 RV1126B Linux 开发板资料页](http://www.openedv.com/docs/boards/arm-linux/RV1126B%20Linux.html)
- [开发板资料下载和讨论帖（旧链接目前已失效）](http://openedv.com/thread-353719-1-1.html)
- [正点原子教学视频主页](https://space.bilibili.com/394620890)

论坛帖给出的整板 A 盘资料链接为：

- 百度网盘：<https://pan.baidu.com/s/19LFDWw4NwikkdH8neFWVJA?pwd=bpmv>
- 提取码：`bpmv`

厂商提供的 Ubuntu 虚拟机和 SDK 入口为：

- 百度网盘：<https://pan.baidu.com/s/1HPjMir4PdoJxXg_csjJwEA?pwd=egcj>
- 提取码：`egcj`

核心板页面和开发板页面指向的是同一个 B 盘，只需下载一次。其中 `ATK-DLRK3XXX.zip` 是多款 Rockchip 板共用的 Ubuntu 虚拟机环境，`atk_dlrv1126b_linux6.1_sdk...` 是 DLRV1126B 整板专用 SDK；当前本地 B 盘已经完整，无需再下载所谓“核心板 B 盘”。

当前本地状态（2026-07-14）：开发板 A 盘、开发板 B 盘和核心板 A 盘均已完整下载，不需要重复下载。核心板 A 盘与开发板 A 盘分别描述核心板和底板/整机，两者互补。

本地目录：

```text
vendor-resources/alientek/
├── atk-dlrv1126b/
│   ├── development-board-a-disk/
│   └── development-board-b-disk/
└── atk-clrv1126b/
    └── core-board-a-disk/
```

开发板 A 盘已核对为 160 个文件、44 个目录、`19,395,928,907` 字节，与此前记录的官方在线目录一致；未发现 `.downloading`、`.part` 等临时下载文件。A 盘内已经包含快速上手、底板 PDF/AD 原理图、开发板与核心板硬件资料、示例源码、工具、系统镜像和 RK 官方文档。

`09、用户手册/在线文档链接.txt` 只有一个在线地址是厂商自 2026-04-25 起的正常发布方式，不是漏下载。当前在线手册入口：

- [ATK-DLRV1126B 在线文档](https://alientek.yuque.com/nfzuim/tsl6qb)

现阶段没有必须补下的厂商资料。D 盘剩余空间约 44.94 GiB，SDK、虚拟机、源码大包和镜像应继续保持压缩；需要修改设备树、驱动或重编系统时，再把 SDK 放到更大空间展开。

## 你发来的核心板页面

- [正点原子 RV1126B Linux 核心板资料页](http://www.openedv.com/docs/boards/arm-linux/RV1126B%20Linuxhxb.html?highlight=rv1126b)
- [核心板资料包（提取码 `b46m`）](https://pan.baidu.com/s/1fqTMn8ByFNBNW_KFF0nNBg?pwd=b46m)

这个页面标题是“核心板”，而你手里是“核心板 + 底板”的完整开发板。做排针、GPIO、ADC、音频和屏幕实验时，应优先查上面的**开发板**资料和底板原理图；核心板资料用于进一步查看 SoM 引脚和核心板设计。

## 芯片能力

- [瑞芯微 RV1126B 产品页](https://www.rock-chips.com/a/cn/product/RV11xilie/2025/1208/2117.html)
- [瑞芯微 RV1126B 公开 Datasheet](https://opensource.rock-chips.com/images/8/82/Rockchip_RV1126B_Datasheet_V1.4-20250930_%28public%29.pdf)

瑞芯微官方页面列出的核心能力包括四核 Cortex-A53、3 TOPS NPU、多摄像头输入、4K H.264/H.265 编解码等。它适合做 Linux 外设控制、传感器采集、摄像头/视频、边缘 AI、音频和本地屏幕界面；本仓库当前先学基础外设，不直接进入 NPU。

公开 Datasheet 给出的芯片级 SAR ADC 指标是 13 位、正常模拟输入范围 0～1.8 V。底板原理图现已到齐，40P 上的外接 ADC 信号属于 1.8 V 模拟域；不要把 3.3 V GPIO 或 5 V 电源直接接入 ADC。
