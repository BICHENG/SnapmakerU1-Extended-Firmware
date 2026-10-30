#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
python3 "$HERE/apps/patch_fb_http.py" "$1/usr/local/bin/fb-http.py"
SOURCE="$CACHE_DIR/v4l2-mpp"
# The camera overlay builds the pinned MPP and WebRTC dependencies first.
test -f "$SOURCE/deps/libdatachannel/build/libdatachannel-static.a"
TARGET="$SOURCE/apps/stream-screen-webrtc"
mkdir -p "$TARGET"
cp "$SOURCE/apps/stream-webrtc/"{main.cpp,Makefile} "$TARGET/"
cp "$HERE/apps/screen_touch.h" "$TARGET/"
python3 "$HERE/apps/patch_webrtc.py" "$TARGET/main.cpp"
make -C "$TARGET" CXX=aarch64-linux-gnu-g++ TARGET=stream-screen-webrtc install DESTDIR="$1"
export PKG_CONFIG_PATH="$SOURCE/deps/mpp/usr-local/lib/pkgconfig"
aarch64-linux-gnu-gcc -Wall -Wextra -O2 \
  -I"$SOURCE/common" -I"$SOURCE/common/capture-common" \
  $(pkg-config --cflags rockchip_mpp) "$HERE/apps/fb-mpp/main.c" \
  $(pkg-config --libs rockchip_mpp) -o "$1/usr/local/bin/fb-mpp"
