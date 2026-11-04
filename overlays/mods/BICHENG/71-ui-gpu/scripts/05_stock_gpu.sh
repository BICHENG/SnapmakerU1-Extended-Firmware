#!/usr/bin/env bash
set -euo pipefail
test -n "${CREATE_FIRMWARE:-}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
revision=85aa60d18b3d5e5588d7b247abf90198f07c8a63
source="$CACHE_DIR/ui-nanovg-$revision"
if [ ! -d "$source/.git" ]; then
    git init -q "$source"
    git -C "$source" remote add origin https://github.com/lvgl/lvgl.git
fi
if [ "$(git -C "$source" rev-parse HEAD 2>/dev/null || true)" != "$revision" ]; then
    test -z "$(git -C "$source" status --porcelain)"
    test "$(git -C "$source" remote get-url origin)" = https://github.com/lvgl/lvgl.git
    git -C "$source" fetch --depth 1 origin "$revision"
    git -C "$source" checkout --detach FETCH_HEAD
fi
test "$(git -C "$source" rev-parse HEAD)" = "$revision"
git -C "$source" diff --exit-code --quiet
make -C "$HERE/apps/stock-gpu" LVGL_DIR="$source" MALI_LIB="$ROOTFS_DIR/usr/lib/libmali.so.1"
install -Dm755 "$HERE/apps/stock-gpu/libstock-gpu.so" "$ROOTFS_DIR/usr/local/lib/libstock-gpu.so"
install -Dm644 "$source/LICENCE.txt" "$ROOTFS_DIR/usr/share/licenses/ui-gpu/LVGL.txt"
sed '/^#ifndef/,$d' "$source/src/libs/nanovg/nanovg.h" > "$ROOTFS_DIR/usr/share/licenses/ui-gpu/NanoVG.txt"
