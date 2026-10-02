# Snapmaker U1 低噪声风扇与 TMC 工程报告

日期：2026-10-03  
工作区：`C:\Users\Administrator\Desktop\00.POC\00.Snapmaker-Firmware`  
分支：`poc/fan-curves-imu-20261001-latest`  
固件来源：paxx12 最新 `develop` 加本项目功能提交

## 结果摘要

本轮固件已经通过打印机自带 Firmware Config 上传接口升级成功。打印机从 B 槽启动，Klipper ready，Moonraker connected，`failed_components=[]`，Tailscale 地址和状态文件哈希均保持不变。

Firmware Config 已出现 `Tweaks → Fan Curves`，并成功切换到 `Quiet`。当前同时启用 `Max Speed = Balanced` 与 `TMC Reduced Current = Enabled`；Klipper 配置显示 X/Y `run_current=1.0`、`max_velocity=600`、`max_accel=22000`，证明原来的互斥限制已经解除且实际生效。

升级后的 IMU 首轮数据已覆盖待机、主风扇 25/50/100% 和腔体风扇 25/50/100%。随后又在 Stock、Quiet、Balanced 三个设置下完成 45/50/70/90°C 单喷嘴测试。主风扇在工具头 IMU 上接近待机底噪；腔体风扇在 50% 与 100% 时出现清楚的结构振动增长；Stock 的喷嘴风扇结构振动显著高于两个新曲线。IMU 只能说明结构振动，不能当作 `dB(A)` 声压结果。

## 实现

### Fan Curves

Firmware Config 只提供三个用户选项：

- `Quiet`：喷嘴实际温度 50°C 以下为 0，70°C 为 0.10，之后非线性递增到 260°C / 1.00。
- `Balanced`：更早进入低速区，面向散热余量更高的使用场景。
- `Stock`：删除活动覆盖文件，回到 paxx12/Snapmaker 原配置。

活动文件固定为：

```text
/oem/printer_data/config/extended/klipper/20_fan_curves.cfg
```

曲线模板留在 overlay；GUI 不复制温控逻辑。Klipper 原有 `heater_fan` 每秒 callback 继续拥有实际温度、目标温度、多加热器状态、回差、腔体 guard 和最终 PWM 决策。

四个 nozzle fan 的 Quiet 表使用实际喷嘴温度：

```text
50°C   0.00
70°C   0.10
90°C   0.15
110°C  0.22
130°C  0.30
150°C  0.40
170°C  0.52
190°C  0.65
210°C  0.78
230°C  0.90
250°C  1.00
260°C  1.00
```

原厂 45°C 突然全速的直接原因是 `external_temp_guard_range=-15,45` 与 `external_temp_guard_fan_speed=1.0`。Quiet/Balanced 把腔体 guard 调整为 55°C / 0.60，并保留 3°C 回差。

### Max Speed 与 Reduced Current

Firmware Config 原先在 shell 检查中禁止两个独立设置共存。本次只删除两处互斥检查，保留各自原有配置写入逻辑，没有改 TMC2240 寄存器协议、初始化顺序或 MCU 通信。

升级后返回配置：

```text
fan_curves=quiet
max_speed=balanced
tmc_reduce_current=enabled
X run_current=1.0
Y run_current=1.0
max_velocity=600
max_accel=22000
```

证据：`reports/build/settings-final-37019633229.json`、`reports/build/configfile-after-upgrade-37019633229.json`。

## 构建与产物

最终固件来自 fork GitHub Actions，不使用本机残缺依赖构建：

```text
repo=BICHENG/SnapmakerU1-Extended-Firmware
run=37019633229
headSha=3f7af47cf2a40fbb6c810bf46bd468d19f74b872
artifact=extended-build/U1_extended__upgrade.bin
size=252582656
sha256=61916B7947CEC8EA88C440B79BFD696A4ADD03BA861BA304381710C580E03081
UPFILE_VERSION=2.0.0.2053f7af47
```

inner `update.img` SHA256：

```text
F22C01BE405AC83BB4599738F2F3499316C68069CBCA6510ED0E02D0DBF5156C
```

解包检查确认 Fan Curves GUI YAML、Quiet/Balanced 模板、Klipper `temp_speed_table` 覆盖逻辑、完整 Klipper/Fluidd/Mainsail 扩展镜像和 `S99vpn` 均存在；旧错误 `Cannot use both...` 已不存在。

证据：`reports/build/actions-37019633229-manifest.txt`、`reports/build/inspect-actions-37019633229.log`、`reports/build/extract-rootfs-37019633229.log`。

## 保护性备份

最终刷机前备份：

```text
reports/baseline/20261002-224938-final-pre-upgrade/
```

其中包含打印机配置、Tailscale 状态、升级前服务状态和 SHA256。已保存完整回滚固件：

```text
firmware/U1_2.0.0.205_20260914173503_upgrade.bin
```

整个过程没有执行 `tailscale clean`，没有删除 `printer_data`，没有切换 VPN provider。

## 升级时间线

### 弯路一：以 `lava` 直接运行升级脚本

尝试：

```text
/home/lava/bin/systemUpgrade.sh upgrade soc /home/lava/update-37019633229.img
```

`lava` 无权打开 `/dev/block/by-name/uboot_b`，也无权执行 reboot/sysrq。升级没有完成写入，打印机保持可用。

证据：`reports/build/upgrade-37019633229-console.log`。

### 弯路二：给页面上传 inner `update.img`

Firmware Config 的 `/api/upgrade/upload` 由 root 服务接管，但这个入口接收完整 UPFILE。上传 inner `update.img` 后返回：

```text
The input upgrade file is invalid
```

服务自动恢复 Klipper/Moonraker，系统槽没有写入。

证据：`reports/build/upgrade-37019633229-firmware-config-api.log`。

### 正确动作：上传完整 UPFILE

将完整 `U1_extended__upgrade.bin` 上传到：

```text
http://snapmaker-u1/firmware-config/
POST /firmware-config/api/upgrade/upload
```

Firmware Config 的 root 服务解开 UPFILE，执行完整 upgrade all，写入 `uboot_b`、`boot_b`、`system_b`，每项 MD5 校验通过，页面返回：

```text
SUCCESS: Completed successfully
```

以后升级只复用这条页面入口，不直接调用 `systemUpgrade.sh`，也不把 inner `update.img` 交给上传接口。

证据：`reports/build/upgrade-37019633229-page-upload.log`。

## 升级后健康检查

```text
启动槽：android_slotsufix=_b
系统版本：2.0.0
Klipper：ready
Moonraker：connected
failed_components=[]
Tailscale：1.92.5
Tailscale IP：100.70.57.39
tailscaled.state SHA256：c88fde2b9bd93e0f0c54184ed194e588ec330670832ef8321823d7c9c197e1e2
```

Firmware Config 成功写入 `/oem/printer_data/config/extended/klipper/20_fan_curves.cfg` 并重启 Klipper。最终安全状态为所有温度目标 0、主风扇 0、腔体风扇 0、RPM 0。

证据：`reports/build/post-upgrade-37019633229-status.txt`、`reports/build/firmware-config-quiet-37019633229.log`、`reports/imu/20261002-post-upgrade/final-safe-state.json`。

## IMU 方法与结果

传感器：`e0_lis2dw`，约 1594 Hz。每个档位当前只有一次约 7–8 秒样本。分析先去掉每轴均值，再计算三轴动态 RMS、动态峰值和稳定频段频谱。

| 条件 | 动态 RMS mm/s² | 相对待机 |
|---|---:|---:|
| 待机 | 57.862 | 基线 |
| 主风扇 25% | 58.199 | +0.6% |
| 主风扇 50% | 58.652 | +1.4% |
| 主风扇 100% | 58.282 | +0.7% |
| 腔体风扇 25% | 59.216 | +2.3% |
| 腔体风扇 50% | 64.989 | +12.3% |
| 腔体风扇 100% | 75.599 | +30.6% |

首轮判断：

- 主风扇对工具头 IMU 的耦合很弱，当前单次样本无法稳定区分三个档位。
- 腔体风扇在 50% 和 100% 时有明显结构振动增长；100% 的稳定频谱峰值约为 118.88 Hz。
- 这些值只能比较机械振动代理。没有校准麦克风和声压计，因此不报告 `dB(A)`，也不把振动变化直接写成听感响度。

原始数据：`reports/imu/20261002-post-upgrade/*.csv`。  
分析结果：`reports/imu/20261002-post-upgrade/imu-metrics.json`。  
校验：`reports/imu/20261002-post-upgrade/SHA256SUMS`。

## 电源风扇为什么原厂会乱转

原厂配置中的电源风扇是 `[heater_fan power_fan]`，heater 列表包含四个喷嘴。打印机没有把电源板温度传给这个对象；它只使用每个 heater 的实际温度、目标温度和目标数量。

原厂 `temp_speed_table` 为：

```text
9999, 260, 1, 3, 1.0
9999,  45, 1, 1, 0.6
```

这五列分别是实际温度阈值、目标温度阈值、满足条件的 heater 数量、已设置目标的 heater 数量门槛、风扇速度。由于实际温度阈值写成 `9999`，原厂电源风扇基本只看目标温度：任一喷嘴目标严格大于 45°C 就开到 60%，三个目标严格大于 260°C 才开到 100%。目标温度一改变，风扇就随之改变；它没有实际加热功率输入，也没有回差。

现场复核：Stock 下 45°C 点单喷嘴风扇已经约 0.30、约 6100 RPM，电源风扇为 0；50°C 点电源风扇直接为 0.60；70/90°C 点仍为 0.60。这个设计解释了“目标一设就突然很吵”，也解释了仓温把喷嘴被动推到 45°C 后四路喷嘴风扇齐起转。

## 电源风扇的选择

heater 的 `power` 字段确实存在，但它是归一化 PWM 输出，不是经过电流传感器校准的电源瓦数。当前 `temp_speed_table` 五列语法也没有功率列。把它直接映射到电源风扇会把单个 heater 的瞬时 PWM 当成电源板热状态，可靠性和可解释性都较差。

本项目选择实际温度曲线作为电源风扇入口：任一喷嘴实际温度达到 70°C 时以低速启动，降到 65°C 以下才停；随后按实际最高喷嘴温度非线性递增。纯实际温度表启用 5°C 回差，目标温度变化不会单独启动电源风扇。这个策略直接覆盖仓温导致的 45°C 被动升温区，同时保留高温时的散热余量。

新固件中的 Quiet 电源风扇节点从 70°C / 0.10 起，Balanced 从 70°C / 0.12 起；两者都在高温段逐步增加。Stock 仍保留为回滚基线。

三组已完成的现场矩阵：

| 设置 | 45°C | 50°C | 70°C | 90°C |
|---|---|---|---|---|
| Stock | nozzle 0.30 / 约6100 RPM；power 0 | nozzle 0.30；power 0.60 | nozzle 0.40；power 0.60 | nozzle 0.40；power 0.60 |
| Quiet | 所有风扇 0 | 所有风扇 0 | 当前 nozzle 0.10 / 约2047 RPM；power 0 | nozzle 0.15；power 0.10 |
| Balanced | 所有风扇 0 | 所有风扇 0 | 所有风扇 0 | 当前 nozzle 0.10 / 约2031 RPM；power 0.12 |

这组结果把三个选择放在同一条帕累托线上：Stock 保留原厂行为；Quiet 在 70°C 更早保护喷嘴、90°C 电源风扇更低；Balanced 在 45–90°C 测试区间最安静，同时给电源风扇略多余量。新 70°C 电源节点和 5°C 回差必须随下一版固件重新验证。

三组原始数据和状态日志分别保存在 `reports/imu/20261002-stock-thermal/`、`reports/imu/20261002-quiet-thermal/`、`reports/imu/20261002-balanced-thermal/`，每组目录含 `SHA256SUMS`。

## 仍需完成

1. 在包含 70°C 电源节点和 5°C 回差的新固件中，重复 Quiet/Balanced 的 45/50/65/70/90°C 测试。
2. 验证电源风扇从 70°C 启动、在 65°C 以下停止，且四个 nozzle 不会因仓温同时起转。
3. 为待机、主风扇和腔体风扇各档补三次重复样本，再给出均值、离散程度和置信边界。
4. 运行一个短、安全的典型打印窗口，记录运动、温度、风扇、IMU、跳步和异常信息。
5. 若要评价实际噪声，增加固定位置的校准麦克风或声压计；IMU 继续负责结构振动，不替代声学测量。

## 回滚

曲线回滚优先在 Firmware Config 选择 `Stock`。若 Klipper 因曲线文件无法启动，删除：

```text
/oem/printer_data/config/extended/klipper/20_fan_curves.cfg
```

系统固件恢复仍使用 Firmware Config 页面上传完整回滚 UPFILE：

```text
firmware/U1_2.0.0.205_20260914173503_upgrade.bin
```

任何恢复动作都保留 `/home/lava/printer_data/tailscale/` 和 `/userdata` 中现有状态。
