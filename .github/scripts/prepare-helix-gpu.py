#!/usr/bin/env python3
"""Prepare pinned Helix sources and verify the matching U1 GPU artifact."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
VERSION = "1.0.7"
REVISION = "faed4e35010e970ba5612e9b2e109c52a11965a6"
OVERLAY = Path("overlays/mods/BICHENG/71-ui-gpu")
PATCH = OVERLAY / "apps/helix-gpu-shared.patch"
COMMON = OVERLAY / "apps/ui-gpu"
PACKAGE = Path("overlays/firmware-extended/69-app-helixscreen/root/usr/local/share/extended-pkg/helixscreen")
INPUTS = (
    Path(".github/workflows/helix-gpu.yaml"),
    Path(".github/scripts/prepare-helix-gpu.py"),
    PATCH, COMMON / "gpu_output.c", COMMON / "gpu_output.h", PACKAGE,
)
BUILD = {
    "PLATFORM_TARGET": "snapmaker-u1", "DISPLAY_BACKEND": "fbdev",
    "ENABLE_GLES_3D": "yes", "ENABLE_OPENGLES": "no", "SKIP_OPTIONAL_DEPS": "1",
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
    shutil.copyfile(ROOT / COMMON / "gpu_output.c", source / "src/gpu_output.c")
    shutil.copyfile(ROOT / COMMON / "gpu_output.h", source / "include/gpu_output.h")


def binary_info(path):
    with path.open("rb") as binary:
        header = binary.read(64)
    if (len(header) != 64 or header[:6] != b"\x7fELF\x02\x01" or
            struct.unpack_from("<H", header, 16)[0] not in (2, 3) or
            struct.unpack_from("<H", header, 18)[0] != 183):
        raise ValueError("Helix artifact is not an AArch64 little-endian ELF executable")
    return {"name": path.name, "sha256": sha256(path), "bytes": path.stat().st_size}


def checksums(artifact, binary="helix-screen"):
    return "".join(f"{sha256(artifact / name)}  {name}\n"
                   for name in (binary, "VERSION", "source.json"))


def package(source, artifact, self_test=False):
    check_revision(source)
    git(source, "apply", "--reverse", "--check", str(ROOT / PATCH))
    for name, directory in (("gpu_output.c", "src"), ("gpu_output.h", "include")):
        if sha256(source / directory / name) != sha256(ROOT / COMMON / name):
            raise ValueError(f"Helix was built with a different {name}")
    name = "helix-gpu-self-test" if self_test else "helix-screen"
    binary = source / "build/snapmaker-u1/bin" / name
    manifest = identity()
    manifest["binary"] = binary_info(binary)
    artifact.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(binary, artifact / name)
    (artifact / "VERSION").write_text(VERSION + "\n", encoding="utf-8", newline="\n")
    (artifact / "source.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                                           encoding="utf-8", newline="\n")
    (artifact / "SHA256SUMS").write_text(checksums(artifact, name), encoding="utf-8", newline="\n")
    verify(artifact, self_test)


def verify(artifact, self_test=False):
    manifest = json.loads((artifact / "source.json").read_text(encoding="utf-8"))
    expected = identity()
    name = "helix-gpu-self-test" if self_test else "helix-screen"
    expected["binary"] = binary_info(artifact / name)
    if manifest != expected:
        raise ValueError("Helix artifact does not match the pinned source and current GPU inputs")
    if (artifact / "VERSION").read_bytes() != (VERSION + "\n").encode():
        raise ValueError("Helix artifact version mismatch")
    if (artifact / "SHA256SUMS").read_text(encoding="utf-8") != checksums(artifact, name):
        raise ValueError("Helix artifact checksum mismatch")
    print(f"Verified Helix {VERSION} / {REVISION}: {expected['binary']['sha256']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("prepare").add_argument("source", type=Path)
    pack = commands.add_parser("package")
    pack.add_argument("source", type=Path)
    pack.add_argument("artifact", type=Path)
    pack.add_argument("--self-test", action="store_true")
    check = commands.add_parser("verify")
    check.add_argument("artifact", type=Path)
    check.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            prepare(args.source)
        elif args.command == "package":
            package(args.source, args.artifact, args.self_test)
        else:
            verify(args.artifact, args.self_test)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        detail = error.stderr if isinstance(error, subprocess.CalledProcessError) else str(error)
        parser.exit(1, f"Helix GPU: {detail}\n")


if __name__ == "__main__":
    main()
