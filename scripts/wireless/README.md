# Wi-Fi ADB 连接与联网验收

`wireless-adb.ps1` 用于开发阶段在 Windows 上连接 RV1126B。它只管理 ADB 连接并检查网络状态，**不会保存或修改 Wi-Fi 名称、密码，也不会自动安装任何网络软件**。

## 安全提醒

这块开发板上的 ADB 通常具有 `root` 权限。开启 TCP 5555 后，同一局域网中的其他设备可能尝试连接并控制开发板。因此：

- 只在可信、隔离的家庭或实验室局域网使用；
- 不要在校园公共 Wi-Fi、酒店 Wi-Fi 或直接暴露到公网的网络上开启；
- 使用完成后执行 `Disable` 恢复 USB 模式；
- 正式产品不要依赖无认证的无线 ADB，应改用有认证的 SSH/SFTP 或应用自己的鉴权接口。

## 前置条件

1. Windows 已安装 `adb`，并能在 PowerShell 中执行 `adb devices`。
2. 首次启用时用 USB 连接开发板。
3. 先在开发板屏幕上将 Wi-Fi 接入与电脑相同的可信局域网。脚本不会代填 Wi-Fi 密码。
4. 如果开发板使用的无线接口不是 `wlan0`，运行时通过 `-Interface wlan1` 等参数指定。

## 首次启用无线 ADB

脚本会通过 USB 自动读取 `wlan0` 的 IPv4 地址、切换到 TCP 5555、无线连接并进行验收：

```powershell
.\scripts\wireless\wireless-adb.ps1 -Action Enable -Interface wlan0 -AcceptLanRisk
```

存在多个 USB ADB 设备时，指定开发板序列号：

```powershell
.\scripts\wireless\wireless-adb.ps1 -Action Enable -UsbSerial 17236123880fc0c5 -Interface wlan0 -AcceptLanRisk
```

自动读取不到地址时，可以手工指定。此操作仍然需要 USB，因为脚本要先启用板端 TCP ADB：

```powershell
.\scripts\wireless\wireless-adb.ps1 -Action Enable -BoardIp 192.168.1.80 -Interface wlan0 -AcceptLanRisk
```

脚本不会仅凭 `adb connect` 的文字就判断成功；只有对 `IP:端口` 再执行 `get-state` 并得到 `device` 才算连接成功。

## 后续只通过 Wi-Fi 连接

板端已经处于 TCP ADB 模式时，无须插 USB：

```powershell
.\scripts\wireless\wireless-adb.ps1 -Action Connect -BoardIp 192.168.1.80 -Interface wlan0
```

DHCP 地址可能在重启或重新联网后变化。需要长期固定入口时，可以在路由器中为开发板设置 DHCP 地址保留。

连接成功后可以正常使用 ADB，例如：

```powershell
adb -s 192.168.1.80:5555 shell
adb -s 192.168.1.80:5555 push .\sample.wav /userdata/sample.wav
```

## 单独进行联网验收

指定已经连接的无线 ADB 目标：

```powershell
.\scripts\wireless\wireless-adb.ps1 -Action Check -Target 192.168.1.80:5555 -Interface wlan0
```

只有一个 ADB 目标时也可以省略 `-Target`：

```powershell
.\scripts\wireless\wireless-adb.ps1 -Action Check -Interface wlan0
```

需要继续验证真实 DNS 解析和 HTTPS 外网请求时，显式指定探测目标：

```powershell
.\scripts\wireless\wireless-adb.ps1 -Action Check `
  -Target 192.168.1.80:5555 -Interface wlan0 `
  -ProbeHost example.com -ProbeUrl https://example.com/
```

脚本只在提供 `-ProbeHost`/`-ProbeUrl` 时访问相应目标，避免普通状态检查产生意外外网流量。`ProbeUrl` 只接受绝对 `https://` 地址。

验收逐项检查：

- ADB 目标确实处于 `device` 状态；
- 指定接口具有 IPv4 地址；
- 存在默认路由；
- `/etc/resolv.conf` 中存在 DNS 服务器；
- 板端 Unix 时间与电脑时间相差不超过 5 分钟；
- 板端至少存在 `curl` 或 `wget` 之一，具备发起 HTTPS 请求的工具基础；
- 指定 `-ProbeHost` 后实际执行一次 DNS 解析；
- 指定 `-ProbeUrl` 后实际执行一次 HTTPS 请求。

任一项目失败时脚本返回非零退出码，并输出 `[FAIL]`。未提供探测参数时，全部 `[PASS]` 只表示本地配置条件齐全；提供两个探测参数且全部通过，才表示对应域名与 HTTPS 地址在本次检查中可达。脚本不会为了让结果通过而私自校时、改 DNS 或安装工具。

## 关闭无线 ADB并恢复 USB

保持 USB 线连接，然后通过当前无线连接发出恢复命令：

```powershell
.\scripts\wireless\wireless-adb.ps1 -Action Disable -Target 192.168.1.80:5555
```

脚本执行 `adb usb` 后最多等待 10 秒，并使用板端 `ro.serialno` 核对重新出现的 USB 设备。只有确认是同一块开发板才会报告恢复成功；同时连接的手机等其他 USB ADB 设备不会被当成开发板。无法取得板端身份或未匹配成功时，脚本返回非零退出码并提示手工检查。

## Wi-Fi 与蓝牙的用途区别

音频文件、程序和配置文件应通过 USB ADB、Wi-Fi ADB，或后续带鉴权的 SFTP/HTTP 服务传输。蓝牙更适合播放指令、阈值调整和少量状态数据，不适合作为高质量音频文件的主要传输通道；本脚本不配置蓝牙文件传输。
