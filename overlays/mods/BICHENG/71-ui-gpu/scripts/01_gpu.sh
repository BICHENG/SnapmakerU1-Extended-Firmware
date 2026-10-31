#!/usr/bin/env bash
set -euo pipefail
test -n "${CREATE_FIRMWARE:-}"

src="$ROOT_DIR/tmp/kernel"
out="$ROOT_DIR/tmp/kernel-build"
"$src/scripts/config" --file "$out/.config" --module CONFIG_MALI_BIFROST \
  --disable CONFIG_MALI_CSF_SUPPORT --disable CONFIG_MALI_BIFROST_GATOR_SUPPORT \
  --set-str CONFIG_MALI_PLATFORM_NAME rk
make -C "$src" O="$out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
make -C "$src" O="$out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules_prepare
make -j4 -C "$src" O="$out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M=drivers/gpu/arm/bifrost modules
install -m644 "$out/drivers/gpu/arm/bifrost/bifrost_kbase.ko" "$ROOTFS_DIR/lib/modules/"
aarch64-linux-gnu-strip --strip-debug "$ROOTFS_DIR/lib/modules/bifrost_kbase.ko"

# Use the board's existing, always-on GPU regulator and unmodified OPP table.
boot="$BUILD_DIR/boot-gpu"
boot_fit.sh extract "$BOOT_IMG" "$boot"
for dtb in "$boot/fdt.dtb" "$boot/resources/rk-kernel.dtb"; do
  test "$(fdtget -t s "$dtb" /gpu@ff320000 compatible)" = 'arm,mali-bifrost'
  regulator=/i2c@ff200000/pmic@20/regulators/DCDC_REG4
  test "$(fdtget -t s "$dtb" "$regulator" regulator-name)" = vdd_gpu
  supply=$(fdtget -t x "$dtb" "$regulator" phandle)
  fdtput -t x "$dtb" /gpu@ff320000 mali-supply "$supply"
  fdtput -t s "$dtb" /gpu@ff320000 status okay
done
boot_fit.sh repack "$boot" "$BOOT_IMG"

revision=ec78b74adc038f97cf173f4061fe3856efc0dc74
base="https://raw.githubusercontent.com/tsukumijima/libmali-rockchip/$revision"
library="$CACHE_DIR/libmali-g52-g24p0.so"
if [ ! -f "$library" ]; then
  wget -O "$library.tmp" "$base/lib/aarch64-linux-gnu/libmali-bifrost-g52-g24p0-gbm.so"
  mv "$library.tmp" "$library"
fi
echo "bc6be2fd5d8b46bb9d4cb22118a3ff77af1ca46cee1ac12bc78e27f89ba3ea3d  $library" | sha256sum -c -
install -m644 "$library" "$ROOTFS_DIR/usr/lib/libmali.so.1"
for name in libEGL.so.1 libGLESv2.so.2 libgbm.so.1; do
  ln -sf libmali.so.1 "$ROOTFS_DIR/usr/lib/$name"
done
mkdir -p "$ROOTFS_DIR/usr/share/licenses/libmali"
wget -O "$ROOTFS_DIR/usr/share/licenses/libmali/END_USER_LICENCE_AGREEMENT.txt" "$base/END_USER_LICENCE_AGREEMENT.txt"
