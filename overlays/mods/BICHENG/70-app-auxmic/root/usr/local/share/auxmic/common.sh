#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-PackageHomePage: https://github.com/BICHENG/SnapmakerU1-Extended-Firmware
# SPDX-FileCopyrightText: Copyright (c) 2026 BICHENG
#
# Shared helpers for the auxmic mod.  This file is meant to be sourced.

# The address the phone reaches the printer on.  The printer has no mDNS, so
# the phone can only use the address, and the certificate has to name it.
auxmic_address() {
    for interface in wlan0 eth0 enp1s0 usb0; do
        found=$(ip -4 -o addr show dev "$interface" scope global 2>/dev/null \
            | awk '{print $4}' | cut -d/ -f1 | head -1)
        if [ -n "$found" ]; then
            printf '%s\n' "$found"
            return 0
        fi
    done
    printf '\n'
}

auxmic_hostname() {
    hostname 2>/dev/null || printf 'lava\n'
}
