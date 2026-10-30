#!/usr/bin/env python3
"""Build the U1's G-code renderer with EGL while retaining its working fbdev display."""
from pathlib import Path
import sys
root=Path(sys.argv[1])
path=root/'docker/Dockerfile.snapmaker-u1'
s=path.read_text(encoding='utf-8')
anchor='    libdrm-dev:arm64 \\\n'
assert s.count(anchor)==1
s=s.replace(anchor,anchor+'    libegl-dev:arm64 \\\n    libgles-dev:arm64 \\\n    libgbm-dev:arm64 \\\n')
path.write_text(s,encoding='utf-8',newline='\n')
path=root/'src/ui/ui_gcode_viewer.cpp';s=path.read_text(encoding='utf-8')
anchor='    builder.set_budget_tube_sides(budget_config.tube_sides);'
assert s.count(anchor)==1
s=s.replace(anchor,'''    // On the U1, 16-sided tubes exhaust the shared memory before the GPU budget notices.
    // Four sides resolve the subpixel-width extrusions on its 480x320 display.
    budget_config.tube_sides = std::min(budget_config.tube_sides, 4);
    budget_config.simplification_tolerance = std::max(budget_config.simplification_tolerance, 0.05f);
    builder.set_budget_tube_sides(budget_config.tube_sides);''')
path.write_text(s,encoding='utf-8',newline='\n')
path=root/'Makefile';s=path.read_text(encoding='utf-8')
anchor='    # DRM backend requires libdrm for display; libinput for LVGL input drivers'
assert s.count(anchor)==1
s=s.replace(anchor,'    ifeq ($(ENABLE_GLES_3D),yes)\n        LDFLAGS += -lEGL -lGLESv2 -lgbm\n    endif\n'+anchor)
path.write_text(s,encoding='utf-8',newline='\n')
path=root/'lv_conf.h';s=path.read_text(encoding='utf-8')
anchor='#define LV_DEF_REFR_PERIOD  33'
assert s.count(anchor)==1
s=s.replace(anchor,'#define LV_DEF_REFR_PERIOD  22')
path.write_text(s,encoding='utf-8',newline='\n')
