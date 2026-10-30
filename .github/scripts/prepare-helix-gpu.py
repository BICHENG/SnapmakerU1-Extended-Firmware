#!/usr/bin/env python3
"""Build the U1's G-code renderer with EGL while retaining its working fbdev display."""
from pathlib import Path
import sys
root=Path(sys.argv[1])
path=root/'docker/Dockerfile.snapmaker-u1'
s=path.read_text()
anchor='    libdrm-dev:arm64 \\\n'
assert s.count(anchor)==1
s=s.replace(anchor,anchor+'    libegl-dev:arm64 \\\n    libgles-dev:arm64 \\\n    libgbm-dev:arm64 \\\n')
path.write_text(s)
path=root/'Makefile';s=path.read_text()
anchor='    LDFLAGS += $(TARGET_LDFLAGS)'
assert s.count(anchor)==1
s=s.replace(anchor,anchor+'\n    ifeq ($(ENABLE_GLES_3D),yes)\n        LDFLAGS += -lEGL -lGLESv2 -lgbm\n    endif')
path.write_text(s)
path=root/'lv_conf.h';s=path.read_text()
anchor='#define LV_DEF_REFR_PERIOD  33'
assert s.count(anchor)==1
s=s.replace(anchor,'#define LV_DEF_REFR_PERIOD  22')
path.write_text(s)
