#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
python3 "$HERE/apps/patch_fb_http.py" "$1/usr/local/bin/fb-http.py"
SOURCE="$CACHE_DIR/v4l2-mpp"
# The camera overlay builds the pinned MPP and WebRTC dependencies first.
test -f "$SOURCE/deps/libdatachannel/build/libdatachannel-static.a"
gcc -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror \
    -I"$HERE/apps" -I"$ROOT_DIR/common" "$HERE/test/screen-snapshot-test.c" -o "$BUILD_DIR/screen-snapshot-test"
"$BUILD_DIR/screen-snapshot-test"
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror \
    -I"$SOURCE/common" "$HERE/test/screen-stream-test.cpp" -o "$BUILD_DIR/screen-stream-test"
"$BUILD_DIR/screen-stream-test"
CXX=g++ python3 "$HERE/test/check-session-protocol.py" \
    --source "$SOURCE/apps/stream-webrtc/main.cpp" \
    --json-include "$SOURCE/deps/libdatachannel/deps/json/include"
TARGET="$SOURCE/apps/stream-screen-webrtc"
mkdir -p "$TARGET"
cp "$SOURCE/apps/stream-webrtc/"{main.cpp,Makefile} "$TARGET/"
cp "$HERE/apps/"{screen_touch.h,screen_packet.h,screen_stream.h} "$TARGET/"
python3 "$HERE/apps/patch_webrtc.py" "$TARGET/main.cpp"
make -C "$TARGET" CXX=aarch64-linux-gnu-g++ TARGET=stream-screen-webrtc install DESTDIR="$1"
export PKG_CONFIG_PATH="$SOURCE/deps/mpp/usr-local/lib/pkgconfig"
aarch64-linux-gnu-gcc -Wall -Wextra -O2 \
  -I"$SOURCE/common" -I"$SOURCE/common/capture-common" \
  -I"$HERE/apps" -I"$ROOT_DIR/common" \
  $(pkg-config --cflags rockchip_mpp) "$HERE/apps/fb-mpp/main.c" \
  $(pkg-config --libs rockchip_mpp) -o "$1/usr/local/bin/fb-mpp"
