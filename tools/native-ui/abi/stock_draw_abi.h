#ifndef STOCK_DRAW_ABI_H
#define STOCK_DRAW_ABI_H

#include <stddef.h>
#include <stdint.h>

/* These offsets belong only to the vendor 2.1.0.157 binary with this hash. */
#define STOCK_GUI_SHA256 "2cf51b704a1d750f269f4132f108197ea7ec3b4b0865b070b87e150bf9d0e472"
#define STOCK_MAIN_OFFSET 0xe800u
#define STOCK_LV_INIT_OFFSET 0xc8160u
#define STOCK_SW_FILL_OFFSET 0x81070u
#define STOCK_FILL_INIT_OFFSET 0x83560u
#define STOCK_GLOBAL_GOT_OFFSET 0x640f38u
#define STOCK_UNIT_HEAD_OFFSET 0x218u
#define STOCK_UNIT_COUNT_OFFSET 0x220u
#define STOCK_SW_DISPATCH_OFFSET 0x689f0u
#define STOCK_SW_EVALUATE_OFFSET 0x68760u
#define STOCK_SW_DELETE_OFFSET 0x689a0u
#define STOCK_GET_TASK_OFFSET 0x89cb0u
#define STOCK_LAYER_ALLOC_OFFSET 0x89f80u
#define STOCK_DISPATCH_REQUEST_OFFSET 0x89c90u
#define STOCK_CF_ARGB8888 0x10u
#define STOCK_CF_XRGB8888 0x11u

typedef struct { int32_t x1, y1, x2, y2; } stock_area_t;
typedef struct stock_layer stock_layer_t;
typedef struct stock_task stock_task_t;
typedef struct stock_draw_unit stock_draw_unit_t;

typedef struct {
    uint8_t magic, cf;
    uint16_t flags, width, height, stride, reserved;
} stock_image_header_t;

typedef struct {
    stock_image_header_t header;
    uint32_t data_size;
    uint8_t *data;
    void *unaligned_data;
    const void *handlers;
} stock_draw_buf_t;

struct stock_layer {
    stock_draw_buf_t *draw_buf;
    stock_area_t buf_area;
    uint32_t color_format;
    stock_area_t clip_area, physical_clip_area;
    stock_task_t *draw_task_head;
    stock_layer_t *parent, *next;
    uint8_t all_tasks_added;
    void *user_data;
};

struct stock_draw_unit {
    stock_draw_unit_t *next;
    stock_layer_t *target_layer;
    const stock_area_t *clip_area;
    int32_t (*dispatch_cb)(stock_draw_unit_t *, stock_layer_t *);
    int32_t (*evaluate_cb)(stock_draw_unit_t *, stock_task_t *);
    int32_t (*wait_for_finish_cb)(stock_draw_unit_t *);
    int32_t (*delete_cb)(stock_draw_unit_t *);
};

typedef struct {
    stock_draw_unit_t base;
    stock_task_t *task_active;
    uint8_t sync[104], thread[24];
    uint8_t initialized, exit_status;
    uint16_t padding;
    uint32_t index;
} stock_draw_sw_unit_t;

struct stock_task {
    stock_task_t *next;
    uint32_t type;
    stock_area_t area, real_area, clip_original, clip_area;
    volatile int32_t state;
    void *draw_dsc;
    uint8_t preferred_unit, preference_score;
};

typedef struct {
    void *object;
    uint32_t part, id1, id2;
    stock_layer_t *layer;
    size_t dsc_size;
    void *user_data;
} stock_draw_dsc_base_t;

typedef struct {
    stock_draw_dsc_base_t base;
    int32_t radius;
    uint8_t opacity, blue, green, red;
    /* The factory enables complex gradients. This PoC requires all 56 bytes zero. */
    uint8_t gradient[56];
} stock_fill_dsc_t;

typedef void (*stock_sw_fill_fn)(stock_draw_unit_t *, const stock_fill_dsc_t *, const stock_area_t *);
typedef void (*stock_fill_init_fn)(stock_fill_dsc_t *);

#define ABI_OFFSET(type, member, value) _Static_assert(offsetof(type, member) == (value), #type "." #member)
_Static_assert(sizeof(void *) == 8, "The factory ABI is AArch64 LP64");
_Static_assert(sizeof(stock_area_t) == 16, "area size");
_Static_assert(sizeof(stock_image_header_t) == 12, "image header size");
_Static_assert(sizeof(stock_draw_buf_t) == 40, "draw buffer size");
ABI_OFFSET(stock_draw_buf_t, data, 16);
ABI_OFFSET(stock_image_header_t, stride, 8);
_Static_assert(sizeof(stock_layer_t) == 104, "layer size");
ABI_OFFSET(stock_layer_t, color_format, 24);
ABI_OFFSET(stock_layer_t, draw_task_head, 64);
_Static_assert(sizeof(stock_draw_unit_t) == 56, "draw unit size");
ABI_OFFSET(stock_draw_unit_t, dispatch_cb, 24);
ABI_OFFSET(stock_draw_unit_t, evaluate_cb, 32);
ABI_OFFSET(stock_draw_unit_t, delete_cb, 48);
_Static_assert(sizeof(stock_draw_sw_unit_t) == 200, "software draw unit size");
ABI_OFFSET(stock_draw_sw_unit_t, task_active, 56);
ABI_OFFSET(stock_draw_sw_unit_t, initialized, 192);
_Static_assert(sizeof(stock_task_t) == 96, "draw task size");
ABI_OFFSET(stock_task_t, state, 76);
ABI_OFFSET(stock_task_t, draw_dsc, 80);
ABI_OFFSET(stock_task_t, preferred_unit, 88);
_Static_assert(sizeof(stock_fill_dsc_t) == 112, "factory fill descriptor size");
ABI_OFFSET(stock_fill_dsc_t, radius, 48);
ABI_OFFSET(stock_fill_dsc_t, opacity, 52);
ABI_OFFSET(stock_fill_dsc_t, gradient, 56);
#undef ABI_OFFSET

#endif
