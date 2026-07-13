# 官方资料入口

## 与当前整板对应的资料

- [正点原子 RV1126B Linux 开发板资料页](http://www.openedv.com/docs/boards/arm-linux/RV1126B%20Linux.html)
- [开发板资料下载和讨论帖](http://openedv.com/thread-353719-1-1.html)
- [正点原子教学视频主页](https://space.bilibili.com/394620890)

论坛帖给出的整板 A 盘资料链接为：

- 百度网盘：<https://pan.baidu.com/s/19LFDWw4NwikkdH8neFWVJA?pwd=bpmv>
- 提取码：`bpmv`

厂商提供的 Ubuntu 虚拟机和 SDK 入口为：

- 百度网盘：<https://pan.baidu.com/s/1HPjMir4PdoJxXg_csjJwEA?pwd=egcj>
- 提取码：`egcj`

先下载开发板 A 盘中的用户手册、底板原理图和排针图。SDK、虚拟机和系统镜像通常很大，等完成 GPIO/ADC 的手册核对后再按需下载，不要提交到本仓库。

## 你发来的核心板页面

- [正点原子 RV1126B Linux 核心板资料页](http://www.openedv.com/docs/boards/arm-linux/RV1126B%20Linuxhxb.html?highlight=rv1126b)

这个页面标题是“核心板”，而你手里是“核心板 + 底板”的完整开发板。做排针、GPIO、ADC、音频和屏幕实验时，应优先查上面的**开发板**资料和底板原理图；核心板资料用于进一步查看 SoM 引脚和核心板设计。

## 芯片能力

- [瑞芯微 RV1126B 产品页](https://www.rock-chips.com/a/cn/product/RV11xilie/2025/1208/2117.html)
- [瑞芯微 RV1126B 公开 Datasheet](https://opensource.rock-chips.com/images/8/82/Rockchip_RV1126B_Datasheet_V1.4-20250930_%28public%29.pdf)

瑞芯微官方页面列出的核心能力包括四核 Cortex-A53、3 TOPS NPU、多摄像头输入、4K H.264/H.265 编解码等。它适合做 Linux 外设控制、传感器采集、摄像头/视频、边缘 AI、音频和本地屏幕界面；本仓库当前先学基础外设，不直接进入 NPU。

公开 Datasheet 给出的芯片级 SAR ADC 指标是 13 位、正常模拟输入范围 0～1.8 V。表中 3.3 V 是最大输入电压，不应当作正常实验电压；而完整开发板排针前是否带分压或保护，还要以正点原子底板原理图为准。
