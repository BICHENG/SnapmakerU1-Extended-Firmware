#ifndef PAGE_RESOURCE_NATIVE_TYPES_H
#define PAGE_RESOURCE_NATIVE_TYPES_H
#include "../abi/image_abi.h"

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
