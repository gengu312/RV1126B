# RV1126B 嵌入式 Linux 开发项目交接文档

## 1. 文档用途

本文档用于把当前项目背景、硬件情况、学习目标和后续开发计划一次性提供给 Codex，作为后续生成代码、脚本、README 和调试方案的基础上下文。

当前阶段不追求直接完成最终科研项目，而是先建立一套可复用的 RV1126B 开发流程：确认板卡信息、登录 Linux、交叉编译、部署程序、采集摄像头、运行 NPU 推理，最后形成可演示的实时视觉 Demo。

## 2. 项目背景

- 使用者为大学实验室科研助理。
- 教授要求学习瑞芯微 RV1126 系列芯片及其 Linux 开发方式。
- 目前已经拿到一块实体开发板，准备在 Codex 中开始后续开发。
- 该平台主要面向嵌入式视觉、摄像头视频处理和边缘 AI 推理。
- 开发重点不是芯片设计，而是把 Linux、摄像头、图像处理和 AI 模型部署串联起来。

## 3. 当前硬件信息

根据此前购买页面，开发板来自正点原子，核心芯片标注为 RV1126B 或 RV1126BJ，形式为“核心板 + 底板”。购买页面还提供 MIPI 屏幕、IMX415 摄像头及不同内存/存储版本。

目前需要从开发板实物、包装标签或系统信息中进一步确认以下内容：

1. 芯片具体型号：RV1126B 还是 RV1126BJ。
2. 内存和存储配置：例如 1 GB + 8 GB，或 2 GB + 32 GB。
3. 开发板和底板的准确型号及硬件版本号。
4. 当前是否已经配有摄像头模组、屏幕、串口线、电源、网线或 Wi-Fi 天线。
5. 板载存储中是否已经预装可启动 Linux 系统。
6. 厂商提供的是 Buildroot、Debian、Ubuntu 衍生系统还是其他根文件系统。

重要限制：RV1126B 与早期 RV1126 不是完全相同的平台。不要默认老 RV1126 的固件、SDK、设备树、驱动或二进制程序可以直接用于 RV1126B。所有底层开发应优先使用该开发板厂商对应版本的资料和 SDK。

## 4. 项目总目标

最终目标是建立以下完整链路：

```text
电脑端编写与编译代码
        ↓
通过网络、串口或存储介质部署到开发板
        ↓
RV1126B Linux 运行程序
        ↓
采集 MIPI/USB 摄像头画面
        ↓
进行图像预处理与 NPU 推理
        ↓
输出检测框、类别、置信度和帧率
        ↓
保存结果、推流或显示到屏幕
```

优先完成一个最小可运行系统，而不是一开始修改内核、移植驱动或训练复杂模型。

## 5. 分阶段开发计划

### 阶段 0：识别硬件和系统

目标：确认板卡、系统、网络、摄像头节点和已有运行库。

需要完成：

- 通过串口或 SSH 登录开发板。
- 记录系统版本、内核版本、CPU、内存和存储信息。
- 确认网络连接方式和开发板 IP。
- 检查 `/dev/video*`、媒体设备和显示设备。
- 检查系统中是否已有 RKNN、RGA、MPP、OpenCV、GStreamer 或 FFmpeg。
- 保存完整的启动日志和 `dmesg` 输出。

### 阶段 1：建立基础编译和部署流程

目标：能够稳定地把自编写程序放到板子上运行。

需要完成：

- 确认开发板 CPU 架构和交叉编译工具链。
- 创建一个 C/C++ 的 Hello World 工程。
- 使用 CMake 构建。
- 通过 `scp`、U 盘或其他方式部署。
- 编写统一的 `build.sh`、`deploy.sh` 和 `run.sh`。
- 记录动态库依赖和运行环境变量。

### 阶段 2：摄像头采集

目标：能够稳定获得摄像头帧。

需要完成：

- 枚举 V4L2 和 Media Controller 设备。
- 识别摄像头对应的 `/dev/videoX`。
- 获取支持的分辨率、帧率和像素格式。
- 先使用厂商 Demo 或 `v4l2-ctl` 验证摄像头。
- 再编写最小 V4L2 采集程序。
- 将一帧保存为原始数据或常见图像格式。
- 记录 NV12、YUV、RGB/BGR 等格式转换方式。

### 阶段 3：NPU/RKNN 推理

目标：跑通一个官方或轻量模型。

需要完成：

- 确认板端 RKNN Runtime 版本。
- 在电脑端准备与板端 Runtime 匹配的模型转换工具。
- 优先使用厂商已经验证的分类或目标检测模型。
- 跑通图片推理，再接入摄像头实时推理。
- 输出推理耗时、总帧率、模型输入尺寸和内存占用。
- 不要在工具版本和算子兼容性尚未确认前随意使用最新模型。

### 阶段 4：形成科研演示 Demo

目标：形成可持续运行、可展示、可扩展的应用。

建议功能：

- 实时摄像头输入。
- 目标检测或图像分类。
- 在画面上绘制结果。
- 终端输出 FPS 和推理耗时。
- 可选保存截图、录像或推送 RTSP。
- 支持配置摄像头节点、模型路径、阈值和输出方式。
- 程序异常退出后能够输出清晰日志。

## 6. 需要学习的知识

### 6.1 Linux 基础

需要掌握文件、权限、进程、网络、日志和设备节点。常用命令包括：

```bash
ls
cd
cp
mv
rm
cat
grep
find
chmod
ps
top
kill
df -h
free -h
dmesg
ip addr
ssh
scp
```

### 6.2 嵌入式 Linux 基础

需要理解但暂时不要求深入修改：

- U-Boot：负责启动系统。
- Linux Kernel：负责驱动和资源管理。
- RootFS：用户空间文件系统。
- Device Tree：描述硬件连接和资源。
- SDK：包含源码、工具链、构建脚本和示例。
- 交叉编译：在电脑端生成 ARM 平台可执行文件。
- 固件烧录：把系统镜像写入板载存储或 SD 卡。

### 6.3 摄像头与视频

重点概念：

- MIPI CSI、USB UVC、V4L2、Media Controller。
- RGB、BGR、YUV420、NV12 等像素格式。
- 分辨率、帧率、曝光、白平衡和 ISP。
- RGA 图像缩放和格式转换。
- MPP 视频编解码。
- OpenCV、GStreamer、FFmpeg 或厂商媒体框架。

### 6.4 AI 模型部署

需要理解以下流程：

```text
训练框架模型或 ONNX
        ↓
RKNN 转换、量化和兼容性检查
        ↓
生成 .rknn 模型
        ↓
板端 RKNN Runtime 加载
        ↓
图像预处理
        ↓
NPU 推理
        ↓
后处理和结果输出
```

前期重点是“部署已有模型”，不是从零训练模型。

### 6.5 编程与构建工具

建议优先级：

1. Shell：系统检查、构建、部署和启动脚本。
2. C/C++：板端正式程序和 SDK 接口。
3. CMake：统一工程构建。
4. Python：电脑端模型转换、验证和辅助工具；板端是否使用取决于系统环境。

## 7. 首次上电后需要采集的信息

登录开发板后，依次执行并保存输出：

```bash
uname -a
cat /etc/os-release 2>/dev/null
cat /proc/cpuinfo
cat /proc/meminfo | head -30
free -h 2>/dev/null || free
df -h
mount
ip addr
ip route
ls -l /dev/video* 2>/dev/null
ls -l /dev/media* 2>/dev/null
ls -l /dev/dri/* 2>/dev/null
dmesg > /tmp/dmesg-full.txt
dmesg | grep -Ei "rknn|npu|rga|mpp|video|camera|mipi|isp|sensor"
which gcc g++ cmake make python3 v4l2-ctl ffmpeg gst-launch-1.0
find /usr /lib /opt -iname "*rknn*" -o -iname "*rga*" -o -iname "*mpp*" 2>/dev/null | head -200
```

建议额外收集：

- 开发板正反面清晰照片。
- 核心板和底板丝印。
- 启动串口完整日志。
- 厂商资料包目录截图。
- SDK、系统镜像和工具链文件名。
- 摄像头模组型号和排线连接位置。

## 8. 需要向厂商或教授确认的事项

1. 后续项目是否必须兼容某个已有 RV1126 工程，还是允许使用 RV1126B。
2. 是否有指定的 Linux 镜像、内核、SDK 和 RKNN Runtime 版本。
3. 项目重点是系统移植、摄像头、AI 推理还是完整视觉应用。
4. 是否需要使用指定摄像头传感器，例如 IMX415。
5. 是否需要本地屏幕显示，还是网络推流即可。
6. 最终模型类型、输入分辨率、目标帧率和精度要求。
7. 是否有现成代码仓库、模型、设备树或驱动需要继承。

## 9. 建议的代码仓库结构

```text
rv1126b-lab/
├── README.md
├── docs/
│   ├── hardware.md
│   ├── system-info.md
│   ├── sdk-notes.md
│   └── troubleshooting.md
├── scripts/
│   ├── collect_board_info.sh
│   ├── build.sh
│   ├── deploy.sh
│   └── run.sh
├── apps/
│   ├── hello_rv1126b/
│   ├── v4l2_probe/
│   ├── camera_capture/
│   └── rknn_demo/
├── config/
│   └── device.env.example
├── models/
│   └── README.md
├── third_party/
└── output/
```

说明：

- 不把大型 SDK、固件和模型直接提交到普通 Git 仓库。
- `device.env` 存放 IP、用户名、部署路径等本机信息，不提交密码。
- 厂商代码尽量保持原样，自己的修改放在独立目录或补丁中。
- 每一步先建立最小可运行示例，再逐渐集成。

## 10. Codex 第一阶段开发任务

Codex 首先应生成一个“环境识别与基础部署工程”，不要直接猜测摄像头 API 或 RKNN 版本。

第一批产物：

1. `README.md`：说明项目目的、已知硬件、未知信息和使用步骤。
2. `scripts/collect_board_info.sh`：自动采集系统、设备节点、网络、库和日志。
3. `apps/hello_rv1126b/`：C++17 + CMake 的最小程序。
4. `scripts/build.sh`：支持本机编译和通过环境变量指定交叉编译器。
5. `scripts/deploy.sh`：通过 SSH/SCP 部署，参数可配置。
6. `scripts/run.sh`：远程运行并打印退出状态。
7. `config/device.env.example`：保存板卡 IP、用户名、端口和部署路径模板。
8. `docs/next-step-checklist.md`：根据采集结果判断下一步应该使用 V4L2、厂商媒体框架还是 RKNN Demo。

编码要求：

- Shell 脚本使用 `set -Eeuo pipefail`，输出明确错误信息。
- 所有路径和设备节点可配置，不硬编码 `/dev/video0`。
- C++ 使用 CMake，默认 C++17。
- 不假定开发板具备 apt、sudo、systemd 或完整 GNU 工具。
- 兼容 BusyBox 环境，无法执行的命令应跳过并记录。
- 未确认工具链前，不写死 `arm-linux-gnueabihf-` 或 `aarch64-linux-gnu-`。
- 未确认芯片和 SDK 前，不引入错误的 RV1126 老版本二进制库。

## 11. 第一阶段验收标准

完成后应满足：

- 一条命令能够采集开发板信息并生成文本报告。
- 能够明确判断 CPU 架构和系统类型。
- 能够成功编译 Hello World。
- 能够把可执行文件部署到开发板并运行。
- 脚本不依赖手工修改大量路径。
- README 能让另一位实验室成员复现相同步骤。
- 所有尚未确认的信息都明确标记为 TODO，而不是由代码猜测。

## 12. 可直接粘贴给 Codex 的初始提示词

```text
请基于下面的项目背景创建一个可运行的代码仓库骨架。

项目：RV1126B/RV1126BJ 嵌入式 Linux 开发板学习与视觉 AI Demo。
使用场景：大学实验室科研助理已经拿到实体开发板，需要先建立可靠的系统识别、交叉编译、部署和运行流程，之后再做摄像头采集与 RKNN/NPU 推理。

当前已知：
1. 开发板来自正点原子，形式为核心板加底板。
2. 芯片页面标注 RV1126B/RV1126BJ，但具体型号、内存、存储、系统版本、CPU 架构和 SDK 版本仍需从实物和系统中确认。
3. 不能默认老 RV1126 SDK、固件、驱动或二进制库兼容 RV1126B。
4. 当前阶段不要修改内核、设备树或摄像头驱动，也不要直接假定 RKNN、RGA、MPP 的具体版本。

请先生成以下内容：
- README.md
- docs/hardware.md
- docs/system-info.md
- docs/next-step-checklist.md
- scripts/collect_board_info.sh
- scripts/build.sh
- scripts/deploy.sh
- scripts/run.sh
- config/device.env.example
- apps/hello_rv1126b/CMakeLists.txt
- apps/hello_rv1126b/src/main.cpp

要求：
- Shell 脚本使用 set -Eeuo pipefail，并提供清晰错误信息。
- collect_board_info.sh 兼容 BusyBox，某条命令不存在时跳过而不是整体失败。
- 自动采集 uname、os-release、cpuinfo、内存、存储、网络、挂载、video/media/dri 设备节点、dmesg 关键词、已安装工具以及 RKNN/RGA/MPP 相关文件。
- build.sh 同时支持本机编译和通过环境变量 TOOLCHAIN_FILE 或 CROSS_COMPILE 指定交叉编译。
- deploy.sh 使用可配置的 SSH_HOST、SSH_USER、SSH_PORT、REMOTE_DIR。
- 不保存密码，不硬编码 IP，不硬编码 /dev/video0。
- C++ 工程使用 C++17 和 CMake，并打印编译时间、架构以及简单的运行信息。
- 对尚未确认的硬件和软件信息使用 TODO 标记。
- 每个文件给出完整内容，不要只给伪代码。

完成仓库骨架后，再说明我需要在开发板上运行哪些命令，以及应该把哪些输出提供回来，以便下一步生成摄像头探测程序。
```

## 13. 后续工作顺序

建议严格按照以下顺序推进：

```text
确认板卡与资料
→ 登录 Linux
→ 采集系统信息
→ 确认工具链
→ 跑通 Hello World
→ 确认摄像头节点
→ 跑通厂商摄像头 Demo
→ 编写 V4L2 采集程序
→ 确认 RKNN Runtime
→ 跑通官方模型
→ 摄像头实时推理
→ 优化性能和稳定性
```

不要跳过“确认型号、SDK 和工具版本”这一步。RV1126B 平台最常见的问题不是业务代码本身，而是错误使用了不匹配的固件、工具链、运行库、模型转换工具或摄像头驱动。
