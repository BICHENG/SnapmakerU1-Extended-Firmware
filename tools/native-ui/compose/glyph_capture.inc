/* Included only by the isolated, hash-checked vendor probe after lv_init. */
#define STOCK_LABEL_INIT_OFFSET 0x85c00u
#define STOCK_LABEL_ITERATE_OFFSET 0x85f90u
#define STOCK_SW_LABEL_OFFSET 0x83440u
#define STOCK_SW_GLYPH_CALLBACK_OFFSET 0x83210u

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
} stock_label_dsc_t;

typedef struct {
    const void *resolved_font;
    uint16_t advance, box_width, box_height;
    int16_t offset_x, offset_y;
    uint16_t padding0;
    uint32_t format;
    uint8_t placeholder, padding1[7];
    uintptr_t glyph_id;
    void *cache_entry;
} stock_font_glyph_dsc_t;

typedef struct {
    void *glyph_data;
    uint32_t format, padding0;
    const stock_area_t *letter_coords, *background_coords;
    const stock_font_glyph_dsc_t *metrics;
    uint8_t color[3], opacity, padding1[4];
    stock_draw_buf_t *scratch_buffer;
} stock_glyph_dsc_t;

typedef struct {
    void *get_glyph_descriptor, *get_glyph_bitmap, *release_glyph;
    int32_t line_height, baseline;
    uint8_t flags;
    int8_t underline_position, underline_thickness;
    uint8_t padding[5];
    const void *descriptor, *fallback, *user_data;
} stock_font_t;

typedef void (*stock_glyph_callback_fn)(stock_draw_unit_t *, stock_glyph_dsc_t *,
                                      stock_fill_dsc_t *, const stock_area_t *);
typedef void (*stock_label_iterate_fn)(stock_draw_unit_t *, const stock_label_dsc_t *,
                                     const stock_area_t *, stock_glyph_callback_fn);
typedef void (*stock_label_draw_fn)(stock_draw_unit_t *, const stock_label_dsc_t *, const stock_area_t *);

#define GLYPH_OFFSET(type, field, offset) _Static_assert(offsetof(type, field) == (offset), #type "." #field)
_Static_assert(sizeof(stock_label_dsc_t) == 128, "factory label descriptor size");
GLYPH_OFFSET(stock_label_dsc_t, text, 48);
GLYPH_OFFSET(stock_label_dsc_t, font, 56);
GLYPH_OFFSET(stock_label_dsc_t, color, 72);
GLYPH_OFFSET(stock_label_dsc_t, line_space, 84);
GLYPH_OFFSET(stock_label_dsc_t, opacity, 100);
GLYPH_OFFSET(stock_label_dsc_t, bidi_direction, 104);
GLYPH_OFFSET(stock_label_dsc_t, decoration_blend_local, 116);
GLYPH_OFFSET(stock_label_dsc_t, hint, 120);
_Static_assert(sizeof(stock_glyph_dsc_t) == 56, "factory glyph draw descriptor size");
GLYPH_OFFSET(stock_glyph_dsc_t, format, 8);
GLYPH_OFFSET(stock_glyph_dsc_t, letter_coords, 16);
GLYPH_OFFSET(stock_glyph_dsc_t, metrics, 32);
GLYPH_OFFSET(stock_glyph_dsc_t, color, 40);
GLYPH_OFFSET(stock_glyph_dsc_t, opacity, 43);
GLYPH_OFFSET(stock_glyph_dsc_t, scratch_buffer, 48);
_Static_assert(sizeof(stock_font_glyph_dsc_t) == 48, "factory glyph metrics size");
GLYPH_OFFSET(stock_font_glyph_dsc_t, format, 20);
GLYPH_OFFSET(stock_font_glyph_dsc_t, glyph_id, 32);
GLYPH_OFFSET(stock_font_glyph_dsc_t, cache_entry, 40);
_Static_assert(sizeof(stock_font_t) == 64, "factory font size");
GLYPH_OFFSET(stock_font_t, line_height, 24);
#undef GLYPH_OFFSET

enum { GLYPH_MAX_EVENTS = 512, GLYPH_GUARD = 64 };
typedef struct {
    bool has_glyph, has_fill;
    stock_glyph_dsc_t glyph;
    stock_font_glyph_dsc_t metrics;
    stock_area_t letter, background, fill_area, clip;
    stock_fill_dsc_t fill;
    stock_draw_buf_t bitmap;
    uint8_t *pixels;
    size_t bytes;
    uintptr_t original_bitmap;
    uint64_t bitmap_hash;
} glyph_probe_event_t;

typedef struct {
    stock_glyph_callback_fn original;
    unsigned count, glyph_count, bitmap_count, fill_count, reused_bitmap_count;
    size_t bitmap_bytes;
    glyph_probe_event_t events[GLYPH_MAX_EVENTS];
} glyph_probe_capture_t;

static glyph_probe_capture_t *glyph_capture;

static uint64_t glyph_hash(const uint8_t *bytes, size_t size)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for(size_t i = 0; i < size; i++) hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    return hash;
}

static void glyph_collect_callback(stock_draw_unit_t *unit, stock_glyph_dsc_t *glyph,
                                   stock_fill_dsc_t *fill, const stock_area_t *fill_area)
{
    glyph_probe_capture_t *capture = glyph_capture;
    if(!capture || capture->count == GLYPH_MAX_EVENTS) fatal("glyph_callback_capacity_exceeded");
    glyph_probe_event_t *event = &capture->events[capture->count++];
    event->clip = *unit->clip_area;
    if(glyph) {
        if(!glyph->letter_coords || !glyph->metrics) fatal("glyph_descriptor_missing_coordinates_or_metrics");
        event->has_glyph = true;
        event->glyph = *glyph;
        event->metrics = *glyph->metrics;
        event->metrics.cache_entry = NULL;
        event->letter = *glyph->letter_coords;
        event->glyph.letter_coords = &event->letter;
        event->glyph.metrics = &event->metrics;
        event->glyph.scratch_buffer = NULL;
        if(glyph->background_coords) {
            event->background = *glyph->background_coords;
            event->glyph.background_coords = &event->background;
        }
        capture->glyph_count++;
        if(glyph->format == 1 || glyph->format == 2 || glyph->format == 4 || glyph->format == 8) {
            event->metrics.glyph_id = (uint32_t)glyph->metrics->glyph_id;
            const stock_draw_buf_t *bitmap = glyph->glyph_data;
            if(!bitmap || !bitmap->data || bitmap->header.magic != 0x19 || bitmap->header.cf != 0x0e ||
               bitmap->header.stride < bitmap->header.width || !bitmap->header.width || !bitmap->header.height)
                fatal("glyph_bitmap_is_not_decoded_A8");
            size_t bytes = (size_t)bitmap->header.stride * bitmap->header.height;
            if(bytes > bitmap->data_size || bytes > 1024 * 1024 ||
               event->metrics.box_width != bitmap->header.width || event->metrics.box_height != bitmap->header.height)
                fatal("glyph_bitmap_dimensions_mismatch");
            event->pixels = malloc(bytes);
            if(!event->pixels) fatal("glyph_bitmap_copy_allocation_failed");
            memcpy(event->pixels, bitmap->data, bytes);
            event->bytes = bytes;
            event->bitmap = *bitmap;
            event->bitmap.data = event->pixels;
            event->bitmap.unaligned_data = event->pixels;
            event->bitmap.data_size = (uint32_t)bytes;
            event->glyph.glyph_data = &event->bitmap;
            event->original_bitmap = (uintptr_t)bitmap->data;
            event->bitmap_hash = glyph_hash(event->pixels, bytes);
            for(unsigned i = 0; i + 1 < capture->count; i++) {
                if(capture->events[i].original_bitmap == event->original_bitmap) {
                    capture->reused_bitmap_count++;
                    break;
                }
            }
            capture->bitmap_count++;
            capture->bitmap_bytes += bytes;
        }
        else if(glyph->format == 0) event->glyph.glyph_data = NULL;
        else fatal("default_font_has_unsupported_non_bitmap_glyph");
    }
    if(fill || fill_area) {
        if(!fill || !fill_area) fatal("glyph_fill_callback_pair_incomplete");
        event->has_fill = true;
        event->fill = *fill;
        event->fill_area = *fill_area;
        capture->fill_count++;
    }
    capture->original(unit, glyph, fill, fill_area);
    if(event->pixels) {
        const stock_draw_buf_t *bitmap = glyph->glyph_data;
        if(glyph_hash(bitmap->data, event->bytes) != event->bitmap_hash)
            fatal("software_glyph_callback_changed_source_bitmap");
    }
}
