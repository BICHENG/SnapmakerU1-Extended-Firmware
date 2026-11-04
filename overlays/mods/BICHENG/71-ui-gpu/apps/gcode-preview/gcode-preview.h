/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GCODE_PREVIEW_H
#define GCODE_PREVIEW_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define GCODE_PREVIEW_ABI 2u
#define GCODE_PREVIEW_LIBRARY "/usr/local/lib/libgcode-preview.so"
#define GCODE_PREVIEW_MAX_BYTES (480u * 320u * 4u)
typedef struct GcodePreview GcodePreview;

enum GcodePreviewPhase { GCODE_PREVIEW_EMPTY, GCODE_PREVIEW_LOADING,
                         GCODE_PREVIEW_LOADED, GCODE_PREVIEW_ERROR };
enum GcodePreviewAction {
    GCODE_PREVIEW_RESET,       /* native yaw=-.45, tilt=.75, zoom=1, 3D; follow when available */
    GCODE_PREVIEW_ROTATE,      /* x/y: relative yaw/tilt in radians */
    GCODE_PREVIEW_ZOOM,        /* x: multiplier, final zoom clamped to .3..6 */
    GCODE_PREVIEW_FLAT,        /* x: 0=3D, 1=2D; use native mode-change camera defaults */
    GCODE_PREVIEW_LAYER,       /* x: zero-based layer; cancels following */
    GCODE_PREVIEW_FOLLOW,      /* enables following; only a matching print drives the layer */
    GCODE_PREVIEW_REFRESH
};

typedef struct {
    const char *path;          /* copied by load; canonical readable local file */
    const char *job_filename;  /* optional exact Moonraker name for a downloaded cache file */
    uint32_t width, height;    /* 1..480 x 1..320; full mode requires 480x320 */
    int compact;              /* native compact framing, without the full-screen layer strip */
    int live_only;            /* native thumbnail: display only when matching printing/paused/complete */
    int follow;               /* initial follow intent for a new source; manual layers cancel it */
} GcodePreviewSource;

typedef struct {
    uint32_t width, height, layers, layer;
    float layer_height, yaw, tilt, zoom;
    int phase, compact, paused, flat, following;
    int live_available, live_follow, offline, visible;
    uint32_t tools_used;       /* bitset of tools that actually extrude in parsed paths */
    char message[128];
} GcodePreviewStatus;

typedef struct {
    uint32_t abi, size;
    GcodePreview *(*create)(void); /* no EGL work on the caller; reject UI_GPU_ENABLED=0 */
    void (*destroy)(GcodePreview *); /* cancel parsing, stop/join worker, release its GL context */
    /* Same file identity updates only the viewport, retaining model, camera and manual layer.
       A changed file or failed session reloads; clear + load explicitly resets a session. */
    int (*load)(GcodePreview *, const GcodePreviewSource *);
    void (*clear)(GcodePreview *);
    void (*pause)(GcodePreview *, int); /* parsing may finish while paused; drawing/progress stop */
    int (*viewport)(GcodePreview *, uint32_t width, uint32_t height, int compact, int live_only);
    void (*action)(GcodePreview *, int action, float x, float y);
    /* Nonblocking: -1=busy, -2=invalid buffer, 0=status only, 1=new image copied.
       A NULL pixels pointer only reads status and never consumes an image.
       Pixels have the native B,G,R,A byte layout, top row first. stride=0 means width*4.
       Padding is untouched. Image metadata describes the copied frame, even during input.
       Parsing success sets LOADED; first frame is signalled only by result 1.
       No old-generation image may be returned after load/clear. */
    int (*poll)(GcodePreview *, GcodePreviewStatus *, uint8_t *pixels, size_t capacity, size_t stride);
} GcodePreviewApi;

/* One installed implementation is used by both UIs. NULL means ABI mismatch. */
const GcodePreviewApi *gcode_preview_get_api(uint32_t abi);

#ifdef __cplusplus
}
#endif
#endif
