/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SCREEN_PACKET_H
#define SCREEN_PACKET_H
#include <stdint.h>
#include <string.h>
#include <endian.h>
#define SCREEN_PACKET_MAGIC UINT32_C(0x55495631)
#define SCREEN_PACKET_HEADER 16u
#define SCREEN_PACKET_MAX (2u * 1024u * 1024u)

static inline void screen_packet_header(uint8_t *out, uint32_t size, uint64_t ns)
{
    uint32_t magic = htobe32(SCREEN_PACKET_MAGIC), length = htobe32(size);
    uint64_t timestamp = htobe64(ns);
    memcpy(out, &magic, 4); memcpy(out + 4, &length, 4); memcpy(out + 8, &timestamp, 8);
}
static inline int screen_packet_read(const uint8_t *data, uint32_t *size, uint64_t *ns)
{
    uint32_t magic, length; uint64_t timestamp;
    memcpy(&magic, data, 4); memcpy(&length, data + 4, 4); memcpy(&timestamp, data + 8, 8);
    *size = be32toh(length); *ns = be64toh(timestamp);
    return be32toh(magic) == SCREEN_PACKET_MAGIC && *size > 0 && *size <= SCREEN_PACKET_MAX && *ns;
}
#endif
