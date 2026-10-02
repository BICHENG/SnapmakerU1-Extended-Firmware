# Snapmaker U1 当前板块

更新时间：2026-10-02 22:20（Asia/Shanghai）

## 当前板块

当前工程位于 **D→E 交界：最新 upstream 已更新，等待 Actions 产出最终可刷镜像**。

- A 基础现场：已完成。工作区、分支、pax `develop` 基线、打印机地址、SSH、Tailscale 状态和多份保护性备份已建立。
- B overlay：已完成。Fan Curves、Max Speed/TMC Reduced Current 共存、Klipper table 兼容补丁已经进入分支。
- C 构建：旧 Actions `36907433167` 已完成，但对应旧基线；本轮本机编译已停止，不能把旧产物当成当前分支成品。当前分支已经重放到最新 `origin/develop`，必须重新触发 Actions。
- D 备份：已完成。配置、服务、启动信息和 Tailscale 状态有归档；刷机前仍需以最终产物再做一次短校验。
- E 升级：未完成。当前打印机仍保持 pax12 `2.0.0`，最终 Actions 产物尚未完成升级前全量验收。
- F 运行验证：部分完成。已验证待机 G-code 入口、Quiet 温度曲线、部分风扇档位和 IMU 代理；升级后矩阵尚未重跑。
- G 报告：进行中。工程报告和决策记录已有，最终版本需要补升级前后状态、Max Speed/TMC、Firmware Config 和最终 IMU 对比。

当前唯一正确入口是：**推送最新分支 → Actions 成功 → 校验完整 UPFILE → 刷机前备份 → 升级 → 健康检查 → IMU/功能矩阵 → 报告。** 每一步的输出都写入本文件或 `reports/`，没有结果就不进入下一步。

## 2026-10-02 纠偏记录

本轮曾尝试本机编译，先后补齐 PyYAML、交叉编译器、`dos2unix`、CMake 和 ARM OpenSSL 开发库；本机环境仍需完整依赖链，且不能替代项目已有的 Actions 成品链。构建没有被用于刷机，运行中的本机构建已停止，打印机未被触碰。

真正的版本问题已经确认：本地功能分支最初落后官方 `origin/develop` 的 4 个提交，其中包含 Tailscale 更新。已先更新官方远端，再将两个功能提交重放到最新 `origin/develop`；当前应以这份重放后的分支触发 Actions。fork 远端保留旧提交，因此推送更新时需要使用明确的强制更新，并在推送后立即用远端分支和 Actions run 校验结果。

绕路根因：此前把“本机编译验证”和“最终可刷成品”混为一条路径，又没有在 backlog 顶部持续记录 upstream 基线、产物来源和下一道门槛，导致重复处理本地依赖。后续只接受当前分支对应的 Actions 成品；本机编译仅在 Actions 无法运行时作为源码诊断，不作为刷机来源。

### 现在正在做

1. 将重放后的功能分支推送到 `BICHENG/SnapmakerU1-Extended-Firmware`。
2. 触发并等待官方 `Build` workflow，产物必须对应当前分支提交。
3. 下载新产物，校验完整 UPFILE、`UPFILE_VERSION`、overlay 文件、Klipper 补丁和 SHA256。

### 进入下一步的条件

- Actions 成功，产物来自当前分支提交，不接受旧 run `36907433167`。
- 成品是完整 `U1_extended__upgrade.bin`/UPFILE，能列出 rootfs 与 upgrade 入口。
- 成品内存在 Fan Curves 设置文件、Quiet/Balanced 模板、TMC 互斥检查修复和 `S99vpn`。
- 刷机前重新保存打印机状态、配置、Tailscale 状态和当前可用 `2.0.0` 回滚镜像。

## 已完成且有证据

| 项目 | 状态 | 证据 |
|---|---|---|
| 分支基于 pax 最新 `develop` | VERIFIED | `git log`、`reports/upstream-20261001.md` |
| Firmware Config `Tweaks → Fan Curves → Quiet/Balanced/Stock` | VERIFIED（静态） | `overlays/.../21_settings_tweaks_fan_curves.yaml` |
| Quiet 曲线 50°C 以下关闭、70°C 进入 10% | VERIFIED（现场临时配置） | `reports/imu/20261002-004313/quiet-curve-thermal-test.jsonl` |
| 主风扇待机命令 | VERIFIED | `M106 S64` 返回 `fan.speed=0.25098` |
| 腔体风扇待机命令 | VERIFIED | `M106 P2 S64` 返回 `cavity_fan.speed=0.25098`、约 591 RPM |
| `M106 P0` 的含义 | VERIFIED | 当前 `fan.py` 仅支持无 `P`、`P2`、`P3` |
| 排风待机门控 | VERIFIED | `purifier.power_detected=false`、`power_det_value=3.3` 时 `P3` 保持 0 |
| 四个 nozzle fan 的温控覆盖行为 | VERIFIED | `SET_HEATER_FAN` 在下一次 callback 后回到 0 |
| 45°C 暴力起转根因 | VERIFIED | 原配置 `external_temp_guard_range=-15,45`、guard speed `1.0` |
| `temp_speed_table` 与原 `stepped_temp_table` 冲突 | VERIFIED | `09_fan_temp_speed_table_override.patch` |
| GitHub Actions 固件 | VERIFIED | run `36907433167`、`work/github-actions-36907433167/U1_extended__upgrade.bin` |
| Tailscale 未被现场测试清除 | VERIFIED | 备份中的 `tailscale-state.tgz`、现场节点仍为 `100.70.57.39` |

## Bug 与根因

### 1. 原厂 45°C 全速

- 根因：四个 `heater_fan` 都有腔体 guard；腔体超出 `45°C` 后直接把风扇设为 `1.0`，优先级高于用户想要的低噪声曲线。
- 影响：四个 nozzle fan 齐刷刷启动，待机或预热阶段噪音突增。
- 处理：Quiet/Balanced 将 guard 上限提高到 `55°C`，速度限制到 `0.60`；基础曲线从实际 nozzle 温度决定。
- 状态：Quiet 已现场验证；升级后仍需验证。

### 2. `temp_speed_table` 与 `stepped_temp_table` 互斥

- 根因：原 `heater_fan.py` 发现两个表同时存在就直接抛错；原配置已经带有 `stepped_temp_table`，overlay 再加入 `temp_speed_table` 必然阻止 Klipper 启动。
- 处理：删除错误分支，保留既有 callback，并让 `temp_speed_table` 分支优先执行；未删除原表，保证 Stock 可回退。
- 状态：静态补丁和临时现场加载已验证；最终升级后需验证日志无 `Cannot use both...`。

### 3. Fan Curves 文件缩进导致外部温度字段进入 table

- 根因：`external_temp_sensor` 等字段曾经缩进在五列 table 下，解析器把它们当成 table 行。
- 处理：把外部 guard 字段放回 section 顶层。
- 状态：已修复并在现场恢复 Klippy ready。

### 4. 待机时“风扇不响应”

- 根因：切片文件中的 `M106` 只在 G-code 被执行时生效；待机不会自动重放打印文件。`M106 P0` 也不是主风扇入口。
- 实际入口：无 `P` 的 `M106` 控制主风扇；`P2` 控制 `cavity_fan`；`P3` 进入 purifier 的电源检测门控。
- 另一个限制：`heater_fan` 每秒 callback 重新计算速度，手动 `SET_HEATER_FAN` 只能改变下一次计算，低温时会回到 0。
- 处理：IMU 风扇测试使用 `M106 S...`、`M106 P2 S...` 或 `SET_FAN_SPEED FAN=cavity_fan ...`；不使用 nozzle `heater_fan` 作为待机激励。
- 状态：已现场验证；无需增加常驻后台风扇服务。

### 5. Max Speed 与 TMC Reduced Current 被错误互斥

- 根因：Firmware Config shell 检查把两个独立设置当成互斥项。
- 处理：只删除两处互斥检查，保留两个配置各自的写入逻辑；不改 TMC 寄存器初始化协议。
- 状态：静态代码已改；升级后必须同时开启并查询 X/Y `run_current` 与运动限值。

### 6. IMU 不能直接代表噪声

- 根因：`e0_lis2dw` 只测工具头结构加速度，没有声压校准链路。
- 影响：可以比较风扇引起的结构振动代理，不能写成 `dB(A)`。
- 处理：报告只使用 RMS、P95/P99、峰值和相对变化；声学结论标记为 `INCONCLUSIVE`。

## 未完成顺序

1. 把 Actions 产物复制到 `firmware/`，记录 SHA256、大小、来源 run 和镜像内容清单。
2. 刷机前做最终只读保护检查：当前版本、启动槽位、Klipper/Moonraker 状态、Tailscale 状态摘要、配置包和磁盘空间。
3. 上传同一份已校验 `update.img`，执行官方 `systemUpgrade.sh upgrade soc`。
4. 重启后只读验收：`/etc/VERSION`、`server/info`、`failed_components=[]`、Moonraker、Fluidd/Mainsail、Firmware Config、Tailscale。
5. 同时启用 Max Speed 与 TMC Reduced Current，查询 `tmc2240 stepper_x/y.run_current` 和 `toolhead` 限值。
6. 在最终固件中切换 Quiet，重跑 50/70/75/100°C 曲线和四 nozzle 非联动检查。
7. 用待机直接 G-code 重跑主风扇、腔体风扇各档 IMU；对排风明确记录 purifier 门控，不把失败写成硬件结论。
8. 运行安全的典型打印窗口，收集运动、温度、IMU、风扇状态和异常信息。
9. 完成报告：每条结论标 `VERIFIED`、`INCONCLUSIVE` 或 `NOT VERIFIED`，附原始文件和回滚路径。

## TDA、寄存器与薄薄露出

- **TDA**：风扇曲线的完整上下文在 `heater_fan.py` 的每秒 callback：它同时拥有当前温度、目标温度、多个 heater、外部腔体 guard、回差和最终 PWM。曲线决策放在这里，调用者只选择曲线文件。
- **寄存器/注册表**：Klipper 的风扇入口由对象注册和 mux command 决定：`fan.py` 注册无 `P`、`P2`、`P3` 映射；`heater_fan.py` 通过 `SET_HEATER_FAN FAN=<name>` 注册每个温控风扇。不要在 GUI 或新 daemon 中复制这些映射。
- **薄薄露出**：Firmware Config 只露出 `Quiet/Balanced/Stock` 三个选择，实际 table 留在模板文件；`20_fan_curves.cfg` 是唯一活动文件，Stock 通过删除它回到原厂。
- **内联消除开销**：不新增轮询服务、IPC、常驻线程或第二份风扇状态。复用 Klipper 已有 1 秒 callback 和 Firmware Config 的重启动作；新增逻辑只发生在已有 table 分支，避免把温度/回差/门控重新复制到外层。
- **边界**：MCU 风扇 PWM 和 TMC 寄存器协议不改；本次改动只调整 Klipper 用户态配置决策和 Firmware Config 互斥检查。

## 验证门槛与回滚

- 刷机门槛：产物来自 Actions 且 SHA256 已记录；镜像完整；刷机前备份可解包；Tailscale 状态摘要已保存。
- 健康门槛：Klippy ready、Moonraker connected、`failed_components=[]`、Firmware Config 可访问、Tailscale 节点在线。
- 功能门槛：Quiet 实际温度曲线成立；Max Speed 与 Reduced Current 同时生效；四 nozzle 不因 45°C guard 一起全速。
- 停止条件：镜像不完整、Klippy 不 ready、Tailscale 消失、MCU 错误、跳步、热失控或风扇无法关闭。
- 回滚顺序：先在 Firmware Config 选 `Stock`；若 Klippy 无法启动，移除 `/oem/printer_data/config/extended/klipper/20_fan_curves.cfg`；若系统升级失败，使用已保存的原厂/已知可用 `update.img` 和官方升级命令恢复；不删除 `/userdata` 下的 Tailscale 状态。

## 结论状态

- `VERIFIED`：代码入口、曲线解析修复、Firmware Config 静态入口、待机主/腔体风扇响应、G-code 映射、备份和 Actions 产物。
- `INCONCLUSIVE`：风扇噪声的声学数值、排风独立振动效果、典型打印下的长期稳定性。
- `NOT VERIFIED`：最终固件升级后的完整健康检查、Max Speed + TMC Reduced Current 同时运行、最终固件上的 Fan Curves GUI 切换和升级后 IMU 对比。
