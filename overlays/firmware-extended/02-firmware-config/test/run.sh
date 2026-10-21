#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-PackageHomePage: https://github.com/paxx12-snapmaker-u1/SnapmakerU1-Extended-Firmware
# SPDX-FileCopyrightText: Copyright (c) 2026 @paxx12
#
# Run the host side checks of this overlay. They need a Python 3 and no
# printer. Every check prints what it did, and the script fails when a check
# fails.

set -euo pipefail

DIR="$(dirname "$(realpath "$0")")"
cd "$DIR"

python3 fan_curves_test.py
python3 heater_fan_test.py
