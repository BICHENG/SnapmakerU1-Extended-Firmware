#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-PackageHomePage: https://github.com/BICHENG/SnapmakerU1-Extended-Firmware
# SPDX-FileCopyrightText: Copyright (c) 2026 BICHENG
#
# Keep the certificate the phone page is served with current.
#
#   auxmic-cert.sh [config-dir]
#
# The page has to come over https, otherwise the browser will not hand it the
# microphone.  The certificate is the printer's own, so the phone has to
# accept it once.  It is made again when it is close to expiring or when the
# printer's address has changed, because a certificate that does not name the
# address the phone uses is one the phone will not open.

set -u

CONFIG_DIR=${1:-/home/lava/printer_data/config/extended/auxmic}
CERT_FILE="$CONFIG_DIR/server.crt"
KEY_FILE="$CONFIG_DIR/server.key"
DAYS=825
RENEW_BEFORE=604800
OWNER=${AUXMIC_CERT_OWNER:-lava}

. /usr/local/share/auxmic/common.sh

certificate_is_current() {
    [ -s "$CERT_FILE" ] && [ -s "$KEY_FILE" ] || return 1
    openssl x509 -in "$CERT_FILE" -noout -checkend "$RENEW_BEFORE" >/dev/null 2>&1 || return 1
    current=$(auxmic_address)
    if [ -n "$current" ]; then
        openssl x509 -in "$CERT_FILE" -noout -text 2>/dev/null \
            | grep -qF "IP Address:$current" || return 1
    fi
    return 0
}

make_certificate() {
    current=$(auxmic_address)
    host=$(auxmic_hostname)
    names="DNS:$host,DNS:lava,IP:127.0.0.1"
    if [ -n "$current" ]; then
        names="$names,IP:$current"
    fi
    mkdir -p "$CONFIG_DIR" || return 1
    rm -f "$CERT_FILE" "$KEY_FILE"
    if ! openssl req -x509 -nodes -newkey rsa:2048 -sha256 -days "$DAYS" \
        -keyout "$KEY_FILE" -out "$CERT_FILE" -subj "/CN=$host" \
        -addext "subjectAltName=$names" >/dev/null 2>&1; then
        echo "auxmic: the certificate could not be made" >&2
        return 1
    fi
    chmod 600 "$KEY_FILE"
    chmod 644 "$CERT_FILE"
    if [ -n "$OWNER" ]; then
        chown "$OWNER" "$KEY_FILE" "$CERT_FILE" 2>/dev/null || true
    fi
    echo "auxmic: the certificate names $names"
    return 0
}

if certificate_is_current; then
    echo "auxmic: the certificate is current"
    exit 0
fi

make_certificate
