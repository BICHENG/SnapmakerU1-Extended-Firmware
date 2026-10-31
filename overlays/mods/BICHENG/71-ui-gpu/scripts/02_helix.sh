#!/usr/bin/env bash
set -euo pipefail
test -n "${CREATE_FIRMWARE:-}"

artifact="${UI_GPU_ARTIFACT_DIR:-$ROOT_DIR/ui-artifact}"
python3 "$ROOT_DIR/.github/scripts/prepare-helix-gpu.py" verify "$artifact"
install -Dm755 "$artifact/helix-screen" "$ROOTFS_DIR/usr/local/libexec/helix-screen-gpu"
install -Dm644 "$artifact/VERSION" "$ROOTFS_DIR/usr/local/libexec/helix-screen-gpu.version"
install -Dm644 "$artifact/source.json" "$ROOTFS_DIR/usr/local/share/ui-gpu/helix-source.json"
install -Dm644 "$artifact/SHA256SUMS" "$ROOTFS_DIR/usr/local/share/ui-gpu/helix-artifact-SHA256SUMS"
