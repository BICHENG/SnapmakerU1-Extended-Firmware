#ifndef NATIVE_IMAGE_ABI_H
#define NATIVE_IMAGE_ABI_H
#include "stock_draw_abi.h"

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
#endif
