#!/usr/bin/env python3
"""Prepare pinned Helix sources and verify the matching U1 GPU artifact."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
VERSION = "1.0.7"
REVISION = "faed4e35010e970ba5612e9b2e109c52a11965a6"
OVERLAY = Path("overlays/mods/BICHENG/71-ui-gpu")
PATCH = OVERLAY / "apps/helix-gpu-shared.patch"
PREVIEW = OVERLAY / "apps/gcode-preview"
DISPLAY = OVERLAY / "apps/ui-display"
GPU = OVERLAY / "apps/ui-gpu"
RENDERER = OVERLAY / "apps/helix-gpu"
MALI_REVISION = "ec78b74adc038f97cf173f4061fe3856efc0dc74"
MALI_SHA256 = "bc6be2fd5d8b46bb9d4cb22118a3ff77af1ca46cee1ac12bc78e27f89ba3ea3d"
MALI_URL = (f"https://raw.githubusercontent.com/tsukumijima/libmali-rockchip/{MALI_REVISION}"
            "/lib/aarch64-linux-gnu/libmali-bifrost-g52-g24p0-gbm.so")
COPIES = (
    (PREVIEW / "gcode-preview.h", "include/gcode-preview.h"),
    (DISPLAY / "ui-display.c", "src/ui-display.c"),
    (DISPLAY / "ui-display.h", "include/ui-display.h"),
    (Path("common/ui-frame.h"), "include/ui-frame.h"),
    (GPU / "gpu_output.c", "src/gpu_output.c"),
    (GPU / "gpu_output.h", "include/gpu_output.h"),
    (RENDERER / "ui_gpu_renderer.c", "src/ui_gpu_renderer.c"),
    (RENDERER / "ui_gpu_renderer.h", "include/ui_gpu_renderer.h"),
    (RENDERER / "ui_gcode_viewer_u1.cpp", "src/ui/ui_gcode_viewer_u1.cpp"),
    *((RENDERER / name, "patches/" + name) for name in (
        "lvgl-font-bitmap-stride.patch", "lvgl-nanovg-correctness.patch",
        "lvgl-nanovg-label-atlas.patch")),
)
PACKAGE = Path("overlays/firmware-extended/69-app-helixscreen/root/usr/local/share/extended-pkg/helixscreen")
INPUTS = (
    Path(".github/workflows/helix-gpu.yaml"),
    Path(".github/scripts/prepare-helix-gpu.py"),
    PATCH, PACKAGE, *(original for original, _ in COPIES),
)
BUILD = {
    "PLATFORM_TARGET": "snapmaker-u1", "DISPLAY_BACKEND": "fbdev",
    "ENABLE_GLES_3D": "no", "ENABLE_OPENGLES": "no", "SKIP_OPTIONAL_DEPS": "1",
    "ENABLE_REMOTE_CONTROL": "no", "ENABLE_MOCKS": "no", "HELIX_PACKAGING": "1",
    "MALI_LIB": "/src/build-support/libmali.so.1",
}


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def identity():
    if f"PKG_VERSION={VERSION}" not in (ROOT / PACKAGE).read_text().splitlines():
        raise ValueError("Helix package version no longer matches the pinned GPU build")
    return {
        "schema": 1,
        "upstream": {"repository": "https://github.com/prestonbrown/helixscreen",
                     "version": VERSION, "revision": REVISION},
        "build": BUILD,
        "mali": {"revision": MALI_REVISION, "sha256": MALI_SHA256},
        "inputs": {path.as_posix(): sha256(ROOT / path) for path in INPUTS},
    }


def git(source, *args, check=True):
    return subprocess.run(["git", "-C", str(source), *args], check=check,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def check_revision(source):
    if git(source, "rev-parse", "HEAD").stdout.strip() != REVISION:
        raise ValueError(f"Helix source must be pinned to {REVISION}")


def prepare(source):
    identity()
    check_revision(source)
    patch = str(ROOT / PATCH)
    if git(source, "apply", "--reverse", "--check", patch, check=False).returncode:
        git(source, "apply", "--check", patch)
        git(source, "apply", patch)
    for original, copied in COPIES:
        shutil.copyfile(ROOT / original, source / copied)
    library = source / "build-support/libmali.so.1"
    library.parent.mkdir(exist_ok=True)
    if not library.exists():
        temporary = library.with_suffix(".part")
        try:
            with urllib.request.urlopen(MALI_URL, timeout=90) as response, temporary.open("wb") as output:
                shutil.copyfileobj(response, output)
            if sha256(temporary) != MALI_SHA256:
                raise ValueError("Mali link library checksum mismatch")
            temporary.replace(library)
        finally:
            temporary.unlink(missing_ok=True)
    if sha256(library) != MALI_SHA256:
        raise ValueError("Mali link library checksum mismatch")


def binary_info(path):
    if path.stat().st_size > 64 * 1024 * 1024:
        raise ValueError("Helix release exceeds 64 MiB; strip debug symbols before packaging")
    with path.open("rb") as binary:
        header = binary.read(64)
    if (len(header) != 64 or header[:6] != b"\x7fELF\x02\x01" or
            struct.unpack_from("<H", header, 16)[0] not in (2, 3) or
            struct.unpack_from("<H", header, 18)[0] != 183):
        raise ValueError("Helix artifact is not an AArch64 little-endian ELF executable")
    return {"name": path.name, "sha256": sha256(path), "bytes": path.stat().st_size}


def checksums(artifact):
    return "".join(f"{sha256(artifact / name)}  {name}\n"
                   for name in ("helix-screen", "VERSION", "source.json"))


def package(source, artifact):
    check_revision(source)
    git(source, "apply", "--reverse", "--check", str(ROOT / PATCH))
    for original, copied in COPIES:
        if sha256(source / copied) != sha256(ROOT / original):
            raise ValueError(f"Helix was built with a different {original}")
    if sha256(source / "build-support/libmali.so.1") != MALI_SHA256:
        raise ValueError("Helix was built with a different Mali link library")
    name = "helix-screen"
    binary = source / "build/snapmaker-u1/bin" / name
    manifest = identity()
    manifest["binary"] = binary_info(binary)
    artifact.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(binary, artifact / name)
    (artifact / "VERSION").write_text(VERSION + "\n", encoding="utf-8", newline="\n")
    (artifact / "source.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                                           encoding="utf-8", newline="\n")
    (artifact / "SHA256SUMS").write_text(checksums(artifact), encoding="utf-8", newline="\n")
    verify(artifact)


def verify(artifact):
    manifest = json.loads((artifact / "source.json").read_text(encoding="utf-8"))
    expected = identity()
    name = "helix-screen"
    expected["binary"] = binary_info(artifact / name)
    if manifest != expected:
        raise ValueError("Helix artifact does not match the pinned source and current GPU inputs")
    if (artifact / "VERSION").read_bytes() != (VERSION + "\n").encode():
        raise ValueError("Helix artifact version mismatch")
    if (artifact / "SHA256SUMS").read_text(encoding="utf-8") != checksums(artifact):
        raise ValueError("Helix artifact checksum mismatch")
    print(f"Verified Helix {VERSION} / {REVISION}: {expected['binary']['sha256']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("prepare").add_argument("source", type=Path)
    pack = commands.add_parser("package")
    pack.add_argument("source", type=Path)
    pack.add_argument("artifact", type=Path)
    check = commands.add_parser("verify")
    check.add_argument("artifact", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            prepare(args.source)
        elif args.command == "package":
            package(args.source, args.artifact)
        else:
            verify(args.artifact)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        detail = error.stderr if isinstance(error, subprocess.CalledProcessError) else str(error)
        parser.exit(1, f"Helix GPU: {detail}\n")


if __name__ == "__main__":
    main()
