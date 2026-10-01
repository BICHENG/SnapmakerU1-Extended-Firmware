# Snapmaker U1 风扇曲线与 TMC 实验报告

日期：2026-10-01

工作区：`C:\Users\Administrator\Desktop\00.POC\00.Snapmaker-Firmware`

分支：`poc/fan-curves-imu-20261001`

## 目标

1. 删除 `Max Speed` 与 `TMC Reduced Current` 的互斥检查。
2. 在 Firmware Config 增加 `Tweaks → Fan Curves`，可切换 `Quiet`、`Balanced`、`Stock`。
3. 用打印机内置 `e0_lis2dw` IMU 比较风扇档位与典型打印状态。

## 已完成

- `Max Speed` 和 `TMC Reduced Current` 的互斥检查已删除。
- 新增 `Fan Curves` 配置项，曲线覆盖 `power_fan` 和四个喷嘴风扇。
- `Stock` 删除 `/oem/printer_data/config/extended/klipper/20_fan_curves.cfg`。
- `Quiet` 的喷嘴风扇从 `50°C` 以下为 `0`，之后非线性递增到 `260°C / 1.00`。
- `Balanced` 的喷嘴风扇从 `60°C` 以下为 `0`，之后非线性递增到 `260°C / 1.00`。
- 腔体保护阈值由原先 `45°C / 100%` 改为 `55°C / 60%`，带 `3°C` 回差。

## 构建

在 WSL 原生目录完成构建：

```text
/home/administrator/00.Snapmaker-Firmware-build
```

产物：

```text
firmware/firmware_extended.bin
firmware/update.img
```

SHA256：

```text
d132f06237b5c0dc1476b3721d2959448f29d602fa96170bdab6e02443860385  firmware/firmware_extended.bin
46879f6a91e27518a3b4601fbe08102eef3a520edca6c09a13f54831015e4d9a  firmware/update.img
```

静态检查通过：rootfs ownership、非 ARM 二进制、squashfs、RK 镜像、UPFILE 重新打包；目标 Firmware Config YAML 与两份曲线文件已进入 rootfs。`tun.ko` 使用打印机当前 `6.1.99` 内核对应模块，保留 Tailscale 所需能力。

构建时 WSL 无法访问多个第三方下载源。为完成打包，临时构建副本使用了打印机现有的 `rsync` 与 `tun.ko`，并移出了若干需要外网下载的非本次目标 overlay（远程屏幕、Fluidd/Mainsail、相机、工具头模拟器、DragonBreath 等）。因此 `firmware_extended.bin` 已验证包含本次风扇/TMC改动，但不能宣称它完整保留了仓库 `extended` profile 的全部应用组件。这是本次刷机后的主要风险，必须在下一轮用完整依赖缓存重新构建并回滚/修复设备后再作为正式固件使用。

## 保护性备份

本地备份：

```text
reports/baseline/20261001-190308/
reports/baseline/20261001-193800/
```

追加保存：

```text
/home/lava/printer_data/tailscale/
/home/lava/printer_data/config/extended/extended2.cfg
```

关键哈希：

```text
c88fde2b9bd93e0f0c54184ed194e588ec330670832ef8321823d7c9c197e1e2  tailscaled.state
dae2ed6dc76d7205f3fdde70b70c930cf4f1c0c69f892d7439a7b482239aa6cc  extended2.cfg
```

没有执行 `tailscale clean`、没有删除 `printer_data`、没有切换 VPN 到 `none`。

## 刷机结果

使用：

```text
/home/lava/bin/systemUpgrade.sh upgrade soc /userdata/update.img
```

打印机返回：

```text
Current slot is B, upgrade A and reboot to active.
rk ota success.
upgrade soc finish, prepare to reboot.
```

`system_a`、`boot_a`、`uboot_a` 写入后的 MD5 校验均通过。随后 SSH 断开，符合切换系统槽位并重启的过程。

截至本报告生成时，打印机仍未恢复 SSH、Moonraker 或 Tailscale 连通性；此前 Tailscale 显示节点离线，局域网地址 `192.168.31.175` 也不可达。因此不能把“升级后服务正常”写成已确认结果，也没有继续发送重刷或恢复命令。

## IMU 数据边界

原机确认：

```text
e0_accelerometer → e0_lis2dw
```

计划使用：

```text
ACCELEROMETER_QUERY CHIP=e0_lis2dw
MEASURE_AXES_NOISE CHIP=e0_lis2dw
ACCELEROMETER_MEASURE CHIP=e0_lis2dw NAME=<trial_id>
```

采样约 `1600 Hz`，原始格式为 `#time,accel_x,accel_y,accel_z`。计划测试静止基线、单风扇 `0/64/128/192/255` 五档、Quiet、Balanced、典型打印窗口，各条件三次，分析 RMS、P99、峰值、峰值频率和相对加速度指数。

本轮没有产生升级后 IMU CSV：刷机后设备离线，不能安全执行采集。IMU 只测工具头机械振动，不能直接等同于声压级或 `dB(A)`。

## 技术判断

原厂 45°C 全速触发来自 `external_temp_guard_range: -15.0, 45.0` 与 `external_temp_guard_fan_speed: 1.0`。这条保护会接管普通温度表，所以腔体达到 45°C 时四个喷嘴风扇一起进入高转速。

`temp_speed_table` 每行五个数依次是：温度阈值、目标温度阈值、满足条件的加热器数量、已设置目标的加热器数量门槛、风扇速度。Klipper 按表格从上到下取第一条满足条件的规则；表格按温度从高到低排列。目标温度阈值写成 `9999` 后，曲线只由实际喷嘴温度触发。外部腔体保护在速度已经大于零时接管风扇速度，并带回差。

## 待完成

- 验证 `Klipper ready`、Moonraker、Firmware Config 页面。
- 在 GUI 中实际切换 `Fan Curves`。
- 同时启用 `Max Speed` 与 `TMC Reduced Current`。
- 采集 IMU 原始 CSV 和风扇状态返回值。
- 运行典型打印，比较温度、稳定性、跳步和振动代理指标。

恢复设备后，第一步只读检查启动槽位、`/etc/VERSION`、网络、`tailscaled.state`、Klipper 日志和 Moonraker 日志；若系统未启动，使用已保存的原始升级包或完整扩展固件回滚，不删除 Tailscale 状态目录。

## 2026-10-01 现场复核：待机 G-code 与 Quiet 曲线

### 待机命令为什么看起来“不响应”

机内 `fan.py` 将主风扇注册为没有 `P` 参数的 `M106 S...`；`M106 P0` 并不代表主风扇，当前实现会记录 `Unsupported fan ID: 0`。`M106 P2` 才映射到 `cavity_fan`，实测 `M106 P2 S128` 返回 `fan_generic cavity_fan.speed = 0.501960...`，转速约 1052 RPM。

`M106 P3` 映射到 `exhaust_fan`，但 `purifier.py` 会先检查 `power_detected`。本机待机时返回 `power_detected=false`、`power_det_value=3.3`，所以它主动拒绝启动；这不是 PWM 状态丢失。`SET_FAN_SPEED FAN=exhaust_fan` 走同一个门控路径。

四个 `heater_fan` 由每秒一次的 `callback()` 重新计算，`SET_HEATER_FAN` 只改变下一次回调使用的 `fan_speed`，温度低于阈值时随后仍会被算回 0。切片常用的是 `M106 S...` 和 `M106 P2 S...`，不是 `M106 P0` 或直接操作喷嘴 `heater_fan`。

### 曲线加载故障与修复

第一次启用 `Quiet` 时 Klipper 未能连接，日志明确报出 `temp_speed_table` 解析到了 `external_temp_sensor: cavity`。根因是覆盖文件把外部温度选项缩进到五列 table 中；已将这四行移到 section 顶层。随后又发现原厂 section 已经有 `stepped_temp_table`，Klipper 原代码会因“两种 table 同时存在”拒绝启动；新增 `09_fan_temp_speed_table_override.patch` 删除这个互斥报错，保留现有 callback 的优先级：有 `temp_speed_table` 时只执行它。

这次故障期间打印机未刷机、未改 MCU、未改 Tailscale；删除运行时曲线文件、清理 root 侧残留 PID 后恢复服务。修复后的 `Quiet` 配置已在机内加载，Moonraker `/server/info` 返回 `klippy_connected=true`、`klippy_state=ready`、`failed_components=[]`。

### Quiet 实测

原始状态逐次记录在 `reports/imu/20261002-004313/quiet-curve-thermal-test.jsonl`。单独将 `extruder` 目标设为 75°C，其他喷嘴目标为 0：

| 喷嘴实际温度 | 当前喷嘴风扇 | 电源风扇 | 观察 |
|---:|---:|---:|---|
| 62°C | 0.00 | 0.00 | 目标温度不直接触发曲线 |
| 75–77°C | 0.10 | 0.00 | 只有当前喷嘴风扇低速运行 |
| 70°C | 0.10 | 0.00 | 回落到 70°C 仍保持该档 |
| 69°C | 0.00 | 0.00 | 低于下一档后关闭 |

这证明当前曲线满足“待机到 45–50°C 不启动；超过 70°C 才以低档开始”的目标，也证明没有把四个喷嘴风扇或电源风扇一起拉起。此处是风扇状态与温度的 VERIFIED 结果；声压仍没有麦克风，IMU 只能继续作为结构振动代理。

### 当前现场 G-code 入口

```text
主风扇：M106 S128 / M106 S0
腔体风扇：M106 P2 S128 / M106 P2 S0
排风：M106 P3 S128（受 purifier 电源检测门控）
通用风扇：SET_FAN_SPEED FAN=cavity_fan SPEED=0.5
喷嘴温控：由 heater_fan callback 控制，不用 SET_HEATER_FAN 作为持续激励
```
