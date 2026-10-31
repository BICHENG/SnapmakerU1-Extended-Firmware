#ifndef NATIVE_PAGE_RESOURCES_H
#define NATIVE_PAGE_RESOURCES_H
#include "../abi/stock_draw_abi.h"
#include <stdbool.h>

enum ResourceKind { RESOURCE_GLYPH_A8=1, RESOURCE_FILL=2, RESOURCE_IMAGE=3 };
enum ResourceError {
    RESOURCE_ERROR_NONE=0,
    RESOURCE_ERROR_CAPACITY=1u<<0,
    RESOURCE_ERROR_BITMAP=1u<<1,
    RESOURCE_ERROR_UNSUPPORTED=1u<<2,
    RESOURCE_ERROR_PADDING=1u<<3,
    RESOURCE_ERROR_CONTEXT=1u<<4
};

/* Events contain no process pointers. Serialize fields explicitly, not struct padding. */
typedef struct {
    uint32_t offset, length;
    uint16_t width, height, stride, flags;
    uint8_t format;
    uint8_t reserved[3];
} ResourceBitmap;

typedef struct {
    uint32_t kind, task_sequence, layer_id;
    stock_area_t area, clip, decoded_clip;
    ResourceBitmap bitmap;
    union {
        struct { uint8_t color[3], opacity; uint32_t format; } glyph;
        struct { int32_t radius; uint8_t color[3], opacity; } fill;
        struct {
            int32_t rotation, scale_x, scale_y, skew_x, skew_y, pivot_x, pivot_y;
            stock_area_t image_area;
            int32_t clip_radius;
            uint8_t recolor[3], recolor_opacity, opacity, flags;
            uint8_t reserved[2];
        } image;
    } data;
} ResourceEvent;

typedef struct {
    ResourceEvent *events;
    uint8_t *bytes;
    uint32_t event_capacity, byte_capacity, event_count, byte_count;
    uint32_t errors, unsupported_events, dropped_events, native_callbacks;
} ResourceCapture;

void resource_capture_init(ResourceCapture *capture, ResourceEvent *events, uint32_t event_capacity,
                           uint8_t *bytes, uint32_t byte_capacity);

/* Returns 0 before drawing for unsupported task/ABI/context: caller uses its native
 * dispatch. Returns 1 once native drawing is consumed, even if capture.errors != 0.
 * Never redraw a consumed task. Capacity/asset errors preserve native rendering.
 * Caller has verified the exact 2.1 ELF SHA, owns an idle native SW unit, has allocated
 * target_layer->draw_buf and set unit->clip_area to task->clip_area. Caller owns task
 * state transitions/dispatch_request and guarantees no concurrent font/image worker.
 * Only LABEL=4 and IMAGE=5 are supported. No lock, allocation, file I/O or scheduling. */
int resource_capture_draw(ResourceCapture *capture, uintptr_t vendor_base,
                          uint32_t task_sequence, uint32_t layer_id,
                          stock_draw_unit_t *unit, const stock_task_t *task);

/* Rebuilds native callback arguments using only event scalars and owned arena bytes.
 * Caller prepares the target layer and refreshing display; no original font/image
 * memory is referenced. Returns false before drawing on an invalid event. */
bool resource_replay_event(uintptr_t vendor_base, stock_draw_unit_t *unit,
                           const ResourceEvent *event, const uint8_t *bytes, uint32_t byte_count);
#endif
