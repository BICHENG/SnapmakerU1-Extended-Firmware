#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
GUI_SHA=$(sha256sum "$ROOTFS_DIR/usr/bin/gui" | cut -d" " -f1)
grep -Fq ".sha256=\"$GUI_SHA\"," "$HERE/apps/stock-ui/stock-abi.h"
make -C "$HERE/apps/stock-ui" CC=aarch64-linux-gnu-gcc
install -Dm755 "$HERE/apps/stock-ui/libstock-ui.so" "$ROOTFS_DIR/usr/local/lib/libstock-ui.so"
