#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
echo "bd2d80e9084c6709161fd9ca4130f06c57e89b00ffa596bc6c5706c4f1aebda4  $ROOTFS_DIR/usr/bin/gui" | sha256sum -c -
make -C "$HERE/apps/stock-ui" test
make -C "$HERE/apps/stock-ui" CC=aarch64-linux-gnu-gcc MALI_LIB="$ROOTFS_DIR/usr/lib/libmali.so.1"
install -Dm755 "$HERE/apps/stock-ui/libstock-ui.so" "$ROOTFS_DIR/usr/local/lib/libstock-ui.so"
