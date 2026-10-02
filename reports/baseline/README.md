# 保护性备份

完整打印机配置和 Tailscale 状态只保存在本机，不上传到 GitHub。

最终刷机前备份目录：

```text
reports/baseline/20261002-224938-final-pre-upgrade/
```

本机目录包含：

- `printer-configs-and-tailscale.tgz`
- `pre-upgrade-status.txt`
- `pre-upgrade-objects.json`
- `SHA256SUMS`
- `STAMP`

已知可用完整回滚固件保存在本机：

```text
firmware/U1_2.0.0.205_20260914173503_upgrade.bin
```

这些文件可能包含设备配置、网络拓扑或认证状态。工程报告只记录必要的哈希、结果和恢复路径。
