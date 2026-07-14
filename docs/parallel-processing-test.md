# RV1126B 并行处理与无线并发初步测试

测试日期：2026-07-14

测试对象：ATK-DLRV1126B 开发板

测试性质：初步共存性验证，不是最终吞吐量验收

## 结论

实机已经证明：四核 CPU 满载、Wi-Fi 主动扫描和蓝牙低功耗广播可以同时运行。15 秒并发期间，CPU 压测正常结束，Wi-Fi 3 次扫描全部成功，蓝牙控制器保持运行且没有 HCI 错误，也没有观察到无线驱动复位或断开。

CPU 单独压测为 `232.62 bogo ops/s`，加入 Wi-Fi 扫描与蓝牙广播后为 `233.87 bogo ops/s`，差异约 `+0.54%`，属于短时测试波动，不能解释为性能提升，也没有显示出有意义的性能下降。

## 官方资料怎样解释并行能力

| 官方信息 | 对并行能力的含义 |
| --- | --- |
| RV1126B 为四核 Cortex-A53，最高 1.6 GHz | Linux SMP 可以把多个可运行任务分配到不同 CPU 核心 |
| 独立 NPU，算力 3 TOPS | 模型推理可由 NPU 执行，与 CPU 上的一般应用并行工作 |
| DMAC 有 2 个物理通道、39 组外设请求、48 个逻辑通道 | 外设数据搬运可由 DMA 协助，减少 CPU 逐字节搬运负担 |
| 底板使用 RTL8733BUUA USB Wi-Fi/蓝牙二合一模块 | Wi-Fi 和蓝牙能共存，但共享模块、USB 2.0 链路和部分射频资源，重负载时可能互相影响 |
| 原理图标出独立 `WIFI ANT` 和 `BT ANT` | 用户安装的两根“小辣椒”天线分别服务 Wi-Fi 与蓝牙 |

因此，开发板不是“同一时刻只能做一件事”。CPU 多核、NPU、DMA 和无线控制器可以并行工作；但并行不代表资源无限，内存带宽、USB 2.0、无线频段和散热仍可能成为瓶颈。

## 实机环境

| 项目 | 检测结果 |
| --- | --- |
| 系统 | Linux 6.1.141，aarch64 |
| CPU | 4 个逻辑 CPU |
| Wi-Fi 接口 | `wlan0`、`wlan1`；本次未关联路由器 |
| USB 无线模块 | Realtek `0bda:b733` |
| 驱动 | `8733bu`、`rtk_btusb` |
| 蓝牙 | `hci0` 为 `UP RUNNING`，HCI/LMP 5.2，Realtek |
| 测试前无线基线 | Wi-Fi 扫到 14 个 BSS；8 秒 BLE 扫描输出 146 行 |
| 测试前温度 | 36.14 °C |

## 方法与结果

先运行 15 秒四核 CPU 压测作为基线；随后开启 BLE 广播，同时再次运行 15 秒四核 CPU 压测，并在压测期间重复执行 Wi-Fi 主动扫描。结束后关闭广播并检查进程、蓝牙状态和错误计数。

| 指标 | CPU 单独压测 | CPU + Wi-Fi + 蓝牙并发 |
| --- | ---: | ---: |
| `stress-ng` 退出状态 | 0 | 0 |
| bogo ops | 3491 | 3522 |
| bogo ops/s（实时） | 232.62 | 233.87 |
| Wi-Fi 扫描 | 未加入 | 3 成功 / 0 失败，累计 52 条 BSS 记录 |
| 蓝牙动作 | 未加入 | BLE 广播启停成功，HCI 错误 0 |
| 温度 | 36.45 → 40.03 °C | 36.72 → 37.93 °C |
| 驱动复位/断开 | 未观察到 | 未观察到 |

测试结束后确认没有遗留 `stress-ng` 进程，`hci0` 仍为 `UP RUNNING`，BLE 广播已关闭。

## 边界与下一步

这次测试回答的是“能不能同时运行”，不能回答“Wi-Fi 和蓝牙同时传大量数据时各有多快”，也没有覆盖 NPU 推理。

要给导师做更严格的第二阶段演示，需要：

1. 开发板连入可控 Wi-Fi，电脑运行 `iperf3`，记录单独 Wi-Fi 和 Wi-Fi+蓝牙并发吞吐。
2. 准备另一台手机或电脑，与开发板建立 BLE/经典蓝牙数据传输并统计速率与丢包。
3. 再加入 RKNN 模型，记录 CPU 占用、NPU 推理帧率、无线吞吐和温度。

## 官方依据

- `vendor-resources/alientek/atk-dlrv1126b/development-board-a-disk/06、硬件资料/02、开发板资料/05、开发板规格书/【正点原子】ATK-DLRV1126B开发板规格书V1.0.pdf`：第 6、11、12、15 页给出开发板资源、无线模块、四核 CPU、3 TOPS NPU 和软件环境。
- `vendor-resources/alientek/atk-dlrv1126b/development-board-a-disk/06、硬件资料/01、核心板资料/02、核心板板载芯片资料/Rockchip RV1126B Datasheet V1.4-20250930.pdf`：第 7、9、11 页给出 CPU、DMAC、NPU 架构说明。
- `vendor-resources/alientek/atk-dlrv1126b/development-board-a-disk/02、开发板原理图/01、底板原理图/ATK-DLRV1126B V1.0.pdf`：第 13 页 RTL8733BUUA、USB 连接及 Wi-Fi/蓝牙天线网络。
- [ATK-DLRV1126B 在线文档](https://alientek.yuque.com/nfzuim/tsl6qb)：SDK、驱动、Wi-Fi、蓝牙、AMP、AI、音视频等章节入口。
