# Snapmaker U1 当前板块

更新时间：2026-10-03 00:58（Asia/Shanghai）

## 当前板块

当前工程位于 **F→G：固件已通过 Firmware Config 上传并升级成功，正在完成运行验证与工程报告**。

- A 基础现场：已完成。工作区、分支、pax `develop` 基线、打印机地址、SSH、Tailscale 状态和多份保护性备份已建立。
- B overlay：已完成。Fan Curves、Max Speed/TMC Reduced Current 共存、Klipper table 兼容补丁已经进入分支。
- C 构建：已完成。fork Actions run `37044194647` 从提交 `922aed2` 构建完整 `U1_extended__upgrade.bin`，大小 `252582656`，SHA256 `8C62B5371CF0AF0E7B94177A592F308A978B94BA801A783C5AEE348EA84AEC74`；产物清单在 `reports/build/actions-37044194647-manifest.txt`。
- D 备份：已完成。最终刷机前备份位于 `reports/baseline/20261002-224938-final-pre-upgrade/`，包含配置、Tailscale 状态、运行状态和校验值；已保存 pax `2.0.0` 完整回滚固件。
- E 升级：已完成。完整 UPFILE 经 Firmware Config `/api/upgrade/upload` 上传，root 升级服务写入并校验 `uboot_b`、`boot_b`、`system_b`，页面返回 `SUCCESS: Completed successfully`。
- F 运行验证：进行中。Klipper、Moonraker、Firmware Config、Tailscale、Quiet、Balanced、Stock、Max Speed 与 Reduced Current 共存、主风扇和腔体风扇 IMU 档位，以及新固件电源风扇的目标预转、实际温度曲线和 65°C 停止点已验证；典型打印窗口仍待补测。
- G 报告：进行中。正在把升级时间线、恢复状态、IMU 原始数据、局限和剩余验证整理到工程报告与决策记录。
- 最新构建门：已通过。Actions run `37044194647` 对提交 `922aed2` 成功生成并发布完整 UPFILE；下一步重新做刷机前备份，再走 Firmware Config 上传和现场验证。

当前已经跑通的升级入口是：**推送最新分支 → Actions 成功 → 校验完整 UPFILE → 刷机前备份 → Firmware Config 页面上传完整 `U1_extended__upgrade.bin` → 健康检查 → IMU/功能矩阵 → 报告。** 后续升级复用这条路径。

## 2026-10-02 纠偏记录

本轮曾尝试本机编译，先后补齐 PyYAML、交叉编译器、`dos2unix`、CMake 和 ARM OpenSSL 开发库；本机环境仍需完整依赖链，且不能替代项目已有的 Actions 成品链。构建没有被用于刷机，运行中的本机构建已停止，打印机未被触碰。

真正的版本问题已经确认：本地功能分支最初落后官方 `origin/develop` 的 4 个提交，其中包含 Tailscale 更新。已先更新官方远端，再将两个功能提交重放到最新 `origin/develop`；当前应以这份重放后的分支触发 Actions。fork 远端保留旧提交，因此推送更新时需要使用明确的强制更新，并在推送后立即用远端分支和 Actions run 校验结果。

绕路根因：此前把“本机编译验证”和“最终可刷成品”混为一条路径，又没有在 backlog 顶部持续记录 upstream 基线、产物来源和下一道门槛，导致重复处理本地依赖。后续只接受当前分支对应的 Actions 成品；本机编译仅在 Actions 无法运行时作为源码诊断，不作为刷机来源。

### 升级路径记录

1. **弯路：SSH 用户直接运行 `systemUpgrade.sh`。** `lava` 无权打开 `/dev/block/by-name/uboot_b`，也无权执行 reboot/sysrq；升级未写入。日志：`reports/build/upgrade-37019633229-console.log`。
2. **弯路：给 Firmware Config 上传 inner `update.img`。** 页面升级入口要求完整 UPFILE，返回 `The input upgrade file is invalid`，随后自动恢复 Klipper/Moonraker；系统槽未写入。日志：`reports/build/upgrade-37019633229-firmware-config-api.log`。
3. **正确动作：给 Firmware Config 上传完整 `U1_extended__upgrade.bin`。** `/firmware-config/api/upgrade/upload` 由 root 服务解包并执行完整升级，三个 B 槽分区写入和 MD5 校验通过，页面返回成功。日志：`reports/build/upgrade-37019633229-page-upload.log`。

### 现在正在做

1. 重新触发当前 `4830e27` 的官方 `Build`，确认完整 UPFILE 与提交一致。
2. 单喷嘴验证 Quiet 在 45/50/69/70/65/64.9/90°C 附近的实际速度与 RPM，其他三个喷嘴目标保持 0，结束后执行 `TURN_OFF_HEATERS`。
3. 对 Quiet、Balanced、Stock 的温控、起转可靠性、低温体验和腔体保护做同口径比较。
4. 运行一个短、安全的典型打印窗口，记录运动、温度、风扇和 IMU，并完成工程报告。

### 2026-10-03 Actions 纠偏

- `37035716572` 的真正失败点是 `10_fan_temp_speed_hysteresis.patch` 两个 hunk 在官方严格 `--fuzz=0` 下无法匹配；前面的 PyYAML 下载提示不是失败原因。
- 根因是 `10` 以 `09` 之前的行结构生成，尤其把回调中的空行当成删除内容；本地宽松 patch 会掩盖这个问题，Actions 才暴露出来。
- `37038484518` 证明两个 hunk 的行位移仍会被官方严格工具拒绝；已删除初始化 hunk，改为回调内 `getattr`，保留一个针对 `09` 后回调的精确 hunk。
- `37039986048` 已成功构建主固件和 AFC 固件；主 UPFILE 已下载、解包、校验 SHA256，下一步执行已有的刷机前备份和 Firmware Config 上传路径。

### 电源风扇判断

- 原厂 `[heater_fan power_fan]` 没有独立电源板温度传感器；它只接收四个喷嘴 heater 的 `current_temp`、`target_temp` 和目标数量。
- 原厂表 `9999,45,1,1,0.6` 的含义是：任一喷嘴目标温度严格大于 45°C 就开到 60%；实际喷嘴温度升到 45°C 不会触发它。它没有实际加热功率输入，也没有温度回差。
- 当前 `temp_speed_table` 五列语法不能读取 heater `power`。heater `power` 是归一化 PWM 输出，不是校准后的电源瓦数；把它直接当电源板热状态会制造错误的安全感。
- 当前选择：电源风扇沿用现有 `temp_speed_table`，改成任一喷嘴实际温度 70°C 起转，65°C 以下停转，速度从 0.10/0.12 向高温递增；新增 5°C 回差只对纯实际温度表生效。
- 新增的低速预转：任一喷嘴目标温度达到 70°C 时，电源风扇只提前运行 Quiet `0.10` / Balanced `0.12`；实际最高喷嘴温度仍负责后续档位，目标清零后按 70/65°C 回差退出。
- 这条预转只解决高温预热期间电源板完全没有气流的时间窗，不把目标温度当作电源板温度，也不宣称拥有电源板级热保护。
- 现场基线：Stock 45°C 喷嘴风扇约 6100 RPM、50°C 电源风扇 0.60；Quiet 70°C 喷嘴 0.10、约 2047 RPM；Balanced 90°C 喷嘴 0.10、约 2031 RPM；四个喷嘴均未联动启动。

## 已完成且有证据

| 项目 | 状态 | 证据 |
|---|---|---|
| 分支基于 pax 最新 `develop` | VERIFIED | `git log`、`reports/upstream-20261001.md` |
| Firmware Config `Tweaks → Fan Curves → Quiet/Balanced/Stock` | VERIFIED（运行） | `reports/build/settings-final-37019633229.json`、`reports/build/firmware-config-quiet-37019633229.log` |
| Quiet 曲线 50°C 以下关闭、70°C 进入 10% | VERIFIED（配置与升级前现场） | `reports/build/configfile-after-upgrade-37019633229.json`、`reports/imu/20261002-004313/quiet-curve-thermal-test.jsonl` |
| 主风扇待机命令 | VERIFIED | `M106 S64` 返回 `fan.speed=0.25098` |
| 腔体风扇待机命令 | VERIFIED | `M106 P2 S64` 返回 `cavity_fan.speed=0.25098`、约 591 RPM |
| `M106 P0` 的含义 | VERIFIED | 当前 `fan.py` 仅支持无 `P`、`P2`、`P3` |
| 排风待机门控 | VERIFIED | `purifier.power_detected=false`、`power_det_value=3.3` 时 `P3` 保持 0 |
| 四个 nozzle fan 的温控覆盖行为 | VERIFIED | `SET_HEATER_FAN` 在下一次 callback 后回到 0 |
| 45°C 暴力起转根因 | VERIFIED | 原配置 `external_temp_guard_range=-15,45`、guard speed `1.0` |
| `temp_speed_table` 与原 `stepped_temp_table` 冲突 | VERIFIED | `09_fan_temp_speed_table_override.patch` |
| GitHub Actions 固件 | VERIFIED | run `37019633229`、`reports/build/actions-37019633229-manifest.txt` |
| 完整 UPFILE 经 Firmware Config 升级 | VERIFIED | `reports/build/upgrade-37019633229-page-upload.log` |
| 升级后 Klipper/Moonraker | VERIFIED | `klippy_state=ready`、`failed_components=[]`，见 `reports/build/post-upgrade-37019633229-status.txt` |
| Max Speed 与 Reduced Current 共存 | VERIFIED | `max_velocity=600`、`max_accel=22000`、X/Y `run_current=1.0`，见 `reports/build/configfile-after-upgrade-37019633229.json` |
| Tailscale 升级后保留 | VERIFIED | `100.70.57.39`，状态哈希仍为 `c88fde...e1e2`，见 `reports/build/post-upgrade-37019633229-status.txt` |
| 新固件 power_fan 目标预转与实际温度曲线 | VERIFIED（单次点测） | `reports/imu/20261003-power-fan-test.jsonl`；目标 69/70、实际 60/131°C 点均符合预期 |
| 升级后 IMU 风扇档位矩阵 | VERIFIED（单次样本） | `reports/imu/20261002-post-upgrade/imu-metrics.json`、原始 CSV 与 `SHA256SUMS` |

## Bug 与根因

### 1. 原厂 45°C 全速

- 根因：四个 `heater_fan` 都有腔体 guard；腔体超出 `45°C` 后直接把风扇设为 `1.0`，优先级高于用户想要的低噪声曲线。
- 影响：四个 nozzle fan 齐刷刷启动，待机或预热阶段噪音突增。
- 处理：Quiet/Balanced 将 guard 上限提高到 `55°C`，速度限制到 `0.60`；基础曲线从实际 nozzle 温度决定。
- 状态：升级后的配置已确认加载；单喷嘴 45/50/70/90°C 运行点仍需补测。

### 2. `temp_speed_table` 与 `stepped_temp_table` 互斥

- 根因：原 `heater_fan.py` 发现两个表同时存在就直接抛错；原配置已经带有 `stepped_temp_table`，overlay 再加入 `temp_speed_table` 必然阻止 Klipper 启动。
- 处理：删除错误分支，保留既有 callback，并让 `temp_speed_table` 分支优先执行；未删除原表，保证 Stock 可回退。
- 状态：升级后 `heater_fan.py` 已包含覆盖逻辑，Klipper ready，原互斥错误未出现。

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
- 状态：运行验证完成。Firmware Config 显示 `max_speed=balanced`、`tmc_reduce_current=enabled`；Klipper 配置显示 X/Y `run_current=1.0`、`max_velocity=600`、`max_accel=22000`。

### 6. IMU 不能直接代表噪声

- 根因：`e0_lis2dw` 只测工具头结构加速度，没有声压校准链路。
- 影响：可以比较风扇引起的结构振动代理，不能写成 `dB(A)`。
- 处理：报告只使用 RMS、P95/P99、峰值和相对变化；声学结论标记为 `INCONCLUSIVE`。
- 新增电源风扇样本：`reports/imu/20261003-powerfan-010/` 记录了 `0.10 → 0.18` 档位转换期间的 8.109 秒 IMU 数据；第一段未能把状态时间与 CSV 时间严格对齐，未纳入结论。

## 未完成顺序

1. 在最终固件中补测 Quiet 的 45/50/70/90°C 单喷嘴温控点和四 nozzle 非联动。
2. 用同一方法比较 Quiet、Balanced、Stock；曲线选择同时看起转可靠性、低温体验、热响应和腔体 guard。
3. 为主风扇和腔体风扇各档补三次重复样本；现有单次样本保留为第一轮结果。
4. 运行安全的典型打印窗口，收集运动、温度、IMU、风扇状态和异常信息。
5. 完成报告：每条结论标 `VERIFIED`、`INCONCLUSIVE` 或 `NOT VERIFIED`，附原始文件和回滚路径。

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
- 回滚顺序：先在 Firmware Config 选 `Stock`；若 Klippy 无法启动，移除 `/oem/printer_data/config/extended/klipper/20_fan_curves.cfg`；若系统需要恢复，仍通过 Firmware Config 页面上传已保存的完整 `firmware/U1_2.0.0.205_20260914173503_upgrade.bin`；不删除 `/userdata` 下的 Tailscale 状态。

## 结论状态

- `VERIFIED`：代码入口、曲线解析修复、Firmware Config 运行入口、完整 UPFILE 升级、升级后健康检查、Max Speed + Reduced Current 共存、待机主/腔体风扇响应、G-code 映射、保护性备份和升级后 IMU 单次矩阵。
- `INCONCLUSIVE`：风扇噪声的声学数值、主风扇的 IMU 区分度、排风独立振动效果、典型打印下的长期稳定性。
- `NOT VERIFIED`：最终固件上的完整 Quiet 温控点、三次重复样本、Quiet/Balanced/Stock 同口径比较和典型打印窗口。
