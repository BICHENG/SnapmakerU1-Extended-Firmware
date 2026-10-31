#!/usr/bin/env bash
set -euo pipefail
test -n "${CREATE_FIRMWARE:-}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
GUI_SHA=$(sha256sum "$ROOTFS_DIR/usr/bin/gui" | cut -d" " -f1)
grep -Fq ".sha256=\"$GUI_SHA\"," "$HERE/apps/stock-display/stock-display.c"
make -C "$HERE/apps/stock-display" test all
install -Dm755 "$HERE/apps/stock-display/libstock-display.so" "$ROOTFS_DIR/usr/local/lib/libstock-display.so"
