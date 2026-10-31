#include "resources.c"
#include <assert.h>
#include <stdio.h>

static unsigned native_images,native_glyphs,native_fills,iterators,tiled_helpers;
static unsigned glyph_mode;
static uint8_t source[32];
static stock_draw_buf_t source_bitmap;
static uint8_t replay_pixels[32];
static bool replaying;
static void image_core(stock_draw_unit_t *unit,const image_draw_dsc_t *dsc,const image_decoder_t *decoder,
                        image_supplement_t *sup,const stock_area_t *area,const stock_area_t *clip)
{
    assert(unit && dsc && decoder && decoder->decoded && sup && area && clip);native_images++;
    if(replaying) {
        assert(dsc->src==NULL && dsc->bitmap_mask==NULL && decoder->cache==NULL);
        assert(dsc->rotation==370 && dsc->scale_x==384 && dsc->scale_y==192);
        memcpy(replay_pixels,decoder->decoded->data,decoder->decoded->data_size);
    }
}
static void image_helper(stock_draw_unit_t *unit,const image_draw_dsc_t *dsc,const stock_area_t *area,image_core_fn callback)
{
    image_decoder_t decoder={.decoded=&source_bitmap};image_supplement_t sup={.alpha_color={1,2,3}};
    callback(unit,dsc,&decoder,&sup,area,unit->clip_area);
    memset(&decoder,0xdd,sizeof(decoder));
}
static void tiled_helper(stock_draw_unit_t *unit,const image_draw_dsc_t *dsc,const stock_area_t *area,image_core_fn callback)
{
    tiled_helpers++;image_helper(unit,dsc,area,callback);image_helper(unit,dsc,area,callback);
}
static void glyph_core(stock_draw_unit_t *unit,ResourceNativeGlyph *glyph,stock_fill_dsc_t *fill,const stock_area_t *fill_area)
{
    assert(unit);native_glyphs++;
    if(fill) {assert(fill_area);native_fills++;}
    if(replaying && glyph) {
        assert(glyph->metrics==NULL && glyph->scratch_buffer==NULL);
        const stock_draw_buf_t *bitmap=glyph->glyph_data;
        memcpy(replay_pixels,bitmap->data,bitmap->data_size);
    }
}
static void label_iterator(stock_draw_unit_t *unit,const ResourceNativeLabel *label,
                           const stock_area_t *area,ResourceGlyphCallback callback)
{
    assert(label);iterators++;
    ResourceNativeMetrics metrics={.box_width=3,.box_height=2,.resolved_font=(void *)1,.cache_entry=(void *)2};
    ResourceNativeGlyph glyph={.glyph_data=&source_bitmap,.format=glyph_mode?9:8,.letter_coords=area,
        .metrics=&metrics,.color={17,34,51},.opacity=117};
    stock_fill_dsc_t fill={.base={.dsc_size=sizeof(fill)},.radius=2,.opacity=91,.blue=7,.green=8,.red=9};
    callback(unit,&glyph,&fill,area);
    memset(&metrics,0xdd,sizeof(metrics));memset(&glyph,0xdd,sizeof(glyph));
}
static void fill_init(stock_fill_dsc_t *fill)
{
    memset(fill,0,sizeof(*fill));fill->base.dsc_size=sizeof(*fill);
}
static void fill_core(stock_draw_unit_t *unit,const stock_fill_dsc_t *fill,const stock_area_t *area)
{
    assert(unit && fill && area);native_fills++;
    assert(fill->opacity==91 && fill->radius==2 && fill->blue==7);
}
static NativeApi api={label_iterator,glyph_core,image_helper,tiled_helper,image_core,fill_init,fill_core};
static void seed(unsigned format)
{
    for(size_t i=0;i<sizeof(source);i++) source[i]=(uint8_t)(i*3+1);
    source_bitmap=(stock_draw_buf_t){.header={.magic=0x19,.cf=(uint8_t)format,.width=3,.height=2,
        .stride=format==IMAGE_CF_A8?4:16},.data_size=format==IMAGE_CF_A8?8:32,.data=source};
    if(format==IMAGE_CF_A8) source[3]=source[7]=0;
}
int main(void)
{
    ResourceEvent events[8];uint8_t arena[256];ResourceCapture capture;
    stock_area_t clip={0,0,63,47};stock_draw_buf_t target={0};
    stock_layer_t layer={.draw_buf=&target};stock_draw_unit_t unit={.target_layer=&layer,.clip_area=&clip};
    image_draw_dsc_t image={.base={.layer=&layer,.dsc_size=sizeof(image)},.src=(void *)1,
        .scale_x=384,.scale_y=192,.rotation=370,.opacity=117,.flags=16};
    stock_task_t task={.type=5,.area={1,2,3,3},.clip_area=clip,.draw_dsc=&image};
    seed(STOCK_CF_ARGB8888);
    uint8_t expected[32];memcpy(expected,source,sizeof(source));
    memset(expected+12,0,4);memset(expected+28,0,4);
    resource_capture_init(&capture,events,8,arena,sizeof(arena));
    assert(draw_with_api(&capture,&api,42,7,&unit,&task)==1);
    assert(!capture.errors && capture.event_count==1 && capture.byte_count==32 && native_images==1);
    assert(events[0].task_sequence==42 && events[0].layer_id==7 && events[0].bitmap.stride==16);
    memset(source,0xdd,sizeof(source));memset(&source_bitmap,0xdd,sizeof(source_bitmap));
    replaying=true;
    assert(replay_with_api(&api,&unit,&events[0],arena,capture.byte_count));
    assert(!memcmp(replay_pixels,expected,32) && unit.clip_area==&clip);
    replaying=false;
    ResourceEvent invalid=events[0];invalid.bitmap.offset=UINT32_MAX;
    assert(!replay_with_api(&api,&unit,&invalid,arena,capture.byte_count) && native_images==2);

    seed(STOCK_CF_XRGB8888);resource_capture_init(&capture,events,8,arena,sizeof(arena));
    assert(draw_with_api(&capture,&api,43,7,&unit,&task)==1 && !capture.errors);
    for(unsigned y=0;y<2;y++) for(unsigned x=0;x<3;x++) assert(arena[y*16+x*4+3]==0);
    resource_capture_init(&capture,events,0,arena,sizeof(arena));
    unsigned before=native_images;
    assert(draw_with_api(&capture,&api,44,7,&unit,&task)==1 && (capture.errors&RESOURCE_ERROR_CAPACITY));
    assert(native_images==before+1 && capture.event_count==0);
    resource_capture_init(&capture,events,8,arena,4);
    assert(draw_with_api(&capture,&api,45,7,&unit,&task)==1 && capture.errors==RESOURCE_ERROR_CAPACITY);
    assert(capture.event_count==0 && capture.byte_count==0);
    resource_capture_init(&capture,events,8,arena,sizeof(arena));image.bitmap_mask=(void *)1;
    assert(draw_with_api(&capture,&api,46,7,&unit,&task)==1 && capture.errors==RESOURCE_ERROR_UNSUPPORTED);
    image.bitmap_mask=NULL;image.flags|=32;resource_capture_init(&capture,events,8,arena,sizeof(arena));
    assert(draw_with_api(&capture,&api,47,7,&unit,&task)==1 && !capture.errors);
    assert(tiled_helpers==1 && capture.event_count==2);

    ResourceNativeLabel label={.base={.layer=&layer,.dsc_size=sizeof(label)},.opacity=255};
    task.type=4;task.draw_dsc=&label;seed(IMAGE_CF_A8);
    uint8_t glyph_expected[8];memcpy(glyph_expected,source,8);
    resource_capture_init(&capture,events,8,arena,sizeof(arena));
    assert(draw_with_api(&capture,&api,50,7,&unit,&task)==1 && !capture.errors);
    assert(capture.event_count==2 && events[0].kind==RESOURCE_GLYPH_A8 && events[1].kind==RESOURCE_FILL);
    memset(source,0xdd,sizeof(source));replaying=true;
    assert(replay_with_api(&api,&unit,&events[0],arena,capture.byte_count));
    assert(!memcmp(replay_pixels,glyph_expected,8));
    assert(replay_with_api(&api,&unit,&events[1],arena,capture.byte_count));replaying=false;
    seed(IMAGE_CF_A8);source[3]=17;resource_capture_init(&capture,events,8,arena,sizeof(arena));before=native_glyphs;
    assert(draw_with_api(&capture,&api,51,7,&unit,&task)==1 && capture.errors==RESOURCE_ERROR_PADDING);
    assert(capture.event_count==0 && native_glyphs==before+1);
    seed(IMAGE_CF_A8);glyph_mode=1;resource_capture_init(&capture,events,8,arena,sizeof(arena));
    assert(draw_with_api(&capture,&api,52,7,&unit,&task)==1 && capture.errors==RESOURCE_ERROR_UNSUPPORTED);
    glyph_mode=0;label.opacity=2;resource_capture_init(&capture,events,8,arena,sizeof(arena));before=iterators;
    assert(draw_with_api(&capture,&api,53,7,&unit,&task)==1 && !capture.errors && !capture.event_count && iterators==before);
    task.type=7;resource_capture_init(&capture,events,8,arena,sizeof(arena));
    assert(draw_with_api(&capture,&api,54,7,&unit,&task)==0 && capture.errors==RESOURCE_ERROR_UNSUPPORTED);
    puts("PASS: owned image/glyph replay, scalar fill, padding/XRGB sanitizing, clipping restore, invalid bounds, tile, opacity, capacity and unsupported pass-through");
    return 0;
}
