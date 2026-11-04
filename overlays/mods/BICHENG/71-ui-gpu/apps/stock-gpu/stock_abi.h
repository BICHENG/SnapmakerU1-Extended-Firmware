#ifndef STOCK_GPU_ABI_H
#define STOCK_GPU_ABI_H

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


#define IMAGE_INIT_OFFSET 0x88e30u
#define IMAGE_SW_OFFSET 0x6c400u
#define IMAGE_CORE_OFFSET 0x6b750u
#define IMAGE_NORMAL_OFFSET 0x89550u
#define IMAGE_TILED_OFFSET 0x88ef0u
#define DISPLAY_CREATE_OFFSET 0xca380u
#define DISPLAY_SET_FORMAT_OFFSET 0xcbe20u
#define REFR_GET_DISPLAY_OFFSET 0x55e50u
#define REFR_SET_DISPLAY_OFFSET 0x55e60u
#define IMAGE_CF_A8 0x0eu

typedef struct {
    stock_draw_dsc_base_t base;
    const void *src;
    stock_image_header_t header;
    int32_t rotation, scale_x, scale_y, skew_x, skew_y;
    int32_t pivot_x, pivot_y;
    uint8_t recolor[3], recolor_opacity, opacity, flags;
    uint8_t reserved[2];
    void *supplement;
    stock_area_t image_area;
    int32_t clip_radius;
    uint32_t padding;
    const void *bitmap_mask;
} image_draw_dsc_t;

typedef struct {
    stock_image_header_t header;
    uint32_t data_size;
    const uint8_t *data;
    const void *reserved;
} image_asset_t;

/* The core callback reads decoded at +72. Other decoder session state is not
 * copied into replay: it belongs to the native helper and its close operation. */
typedef struct {
    uint8_t session_prefix[72];
    const stock_draw_buf_t *decoded;
    const uint32_t *palette;
    uint32_t palette_size, time_to_open;
    void *error_message, *cache, *cache_entry, *user_data;
} image_decoder_t;

typedef struct {
    uint8_t alpha_color[3], padding[5];
    const uint32_t *palette;
    uint32_t palette_size_bits, reserved;
} image_supplement_t;

typedef void (*image_core_fn)(stock_draw_unit_t *, const image_draw_dsc_t *,
    const image_decoder_t *, image_supplement_t *, const stock_area_t *, const stock_area_t *);
typedef void (*image_helper_fn)(stock_draw_unit_t *, const image_draw_dsc_t *,
    const stock_area_t *, image_core_fn);
typedef void (*image_draw_fn)(stock_draw_unit_t *, const image_draw_dsc_t *, const stock_area_t *);

_Static_assert(sizeof(image_draw_dsc_t) == 144, "image descriptor size");
_Static_assert(offsetof(image_draw_dsc_t, src) == 48, "image source");
_Static_assert(offsetof(image_draw_dsc_t, rotation) == 68, "image rotation");
_Static_assert(offsetof(image_draw_dsc_t, scale_x) == 72, "image scale");
_Static_assert(offsetof(image_draw_dsc_t, recolor) == 96, "image recolor");
_Static_assert(offsetof(image_draw_dsc_t, opacity) == 100, "image opacity");
_Static_assert(offsetof(image_draw_dsc_t, supplement) == 104, "image supplement");
_Static_assert(offsetof(image_draw_dsc_t, image_area) == 112, "image area");
_Static_assert(offsetof(image_draw_dsc_t, bitmap_mask) == 136, "image mask");
_Static_assert(sizeof(image_asset_t) == 32, "constant image asset size");
_Static_assert(sizeof(image_decoder_t) == 128, "image decoder session size");
_Static_assert(offsetof(image_decoder_t, decoded) == 72, "decoded draw buffer");
_Static_assert(sizeof(image_supplement_t) == 24, "image supplement size");

typedef struct {
    stock_draw_dsc_base_t base;
    const char *text;
    const void *font;
    uint32_t selection_start, selection_end;
    uint8_t color[3], selection_color[3], selection_background[3], padding0[3];
    int32_t line_space, letter_space, offset_x, offset_y;
    uint8_t opacity, padding1[3];
    uint32_t bidi_direction, align, text_flags;
    uint8_t decoration_blend_local, padding2[3];
    void *hint;
} ResourceNativeLabel;
typedef struct {
    const void *resolved_font;
    uint16_t advance, box_width, box_height;
    int16_t offset_x, offset_y;
    uint16_t padding0;
    uint32_t format;
    uint8_t placeholder, padding1[7];
    uintptr_t glyph_id;
    void *cache_entry;
} ResourceNativeMetrics;
typedef struct {
    void *glyph_data;
    uint32_t format, padding0;
    const stock_area_t *letter_coords, *background_coords;
    const ResourceNativeMetrics *metrics;
    uint8_t color[3], opacity, padding1[4];
    stock_draw_buf_t *scratch_buffer;
} ResourceNativeGlyph;
typedef void (*ResourceGlyphCallback)(stock_draw_unit_t *, ResourceNativeGlyph *,
                                      stock_fill_dsc_t *, const stock_area_t *);
typedef void (*ResourceLabelIterator)(stock_draw_unit_t *, const ResourceNativeLabel *,
                                     const stock_area_t *, ResourceGlyphCallback);
_Static_assert(sizeof(ResourceNativeLabel)==128,"label ABI");
_Static_assert(offsetof(ResourceNativeLabel,opacity)==100,"label opacity ABI");
_Static_assert(sizeof(ResourceNativeGlyph)==56,"glyph ABI");
_Static_assert(offsetof(ResourceNativeGlyph,color)==40,"glyph color ABI");
_Static_assert(sizeof(ResourceNativeMetrics)==48,"glyph metrics ABI");

#endif
