#include "resources.h"
#include "native_types.h"
#include <string.h>

typedef struct {
    ResourceLabelIterator label;
    ResourceGlyphCallback glyph;
    image_helper_fn image, tiled;
    image_core_fn image_core;
    stock_fill_init_fn fill_init;
    stock_sw_fill_fn fill;
} NativeApi;
typedef struct {
    ResourceCapture *capture;
    const NativeApi *api;
    stock_draw_unit_t *unit;
    uint32_t task_sequence, layer_id;
} Invocation;
static _Thread_local Invocation *active;

static NativeApi native_api(uintptr_t base)
{
    return (NativeApi){
        .label=(ResourceLabelIterator)(base+0x85f90u),
        .glyph=(ResourceGlyphCallback)(base+0x83210u),
        .image=(image_helper_fn)(base+IMAGE_NORMAL_OFFSET),
        .tiled=(image_helper_fn)(base+IMAGE_TILED_OFFSET),
        .image_core=(image_core_fn)(base+IMAGE_CORE_OFFSET),
        .fill_init=(stock_fill_init_fn)(base+STOCK_FILL_INIT_OFFSET),
        .fill=(stock_sw_fill_fn)(base+STOCK_SW_FILL_OFFSET)
    };
}
void resource_capture_init(ResourceCapture *capture,ResourceEvent *events,uint32_t event_capacity,
                           uint8_t *bytes,uint32_t byte_capacity)
{
    if(!capture) return;
    *capture=(ResourceCapture){.events=events,.bytes=bytes,.event_capacity=event_capacity,.byte_capacity=byte_capacity};
    if((event_capacity && !events) || (byte_capacity && !bytes)) capture->errors=RESOURCE_ERROR_CONTEXT;
}
static void error(ResourceCapture *capture,uint32_t code)
{
    capture->errors|=code;
    if(code==RESOURCE_ERROR_CAPACITY) capture->dropped_events++;
    else capture->unsupported_events++;
}
static bool area_valid(const stock_area_t *area)
{
    return area && area->x1<=area->x2 && area->y1<=area->y2;
}
static ResourceEvent *event_begin(uint32_t kind)
{
    ResourceCapture *capture=active->capture;
    if(capture->errors) return NULL;
    if(capture->event_count==capture->event_capacity) {error(capture,RESOURCE_ERROR_CAPACITY);return NULL;}
    ResourceEvent *event=&capture->events[capture->event_count];
    memset(event,0,sizeof(*event));event->kind=kind;
    event->task_sequence=active->task_sequence;event->layer_id=active->layer_id;
    event->clip=*active->unit->clip_area;
    return event;
}

static unsigned pixel_bytes(unsigned format)
{
    if(format==IMAGE_CF_A8) return 1;
    if(format==STOCK_CF_ARGB8888 || format==STOCK_CF_XRGB8888) return 4;
    return 0;
}
static bool save_bitmap(ResourceBitmap *out,const stock_draw_buf_t *bitmap,bool glyph)
{
    ResourceCapture *capture=active->capture;
    unsigned bpp=bitmap?pixel_bytes(bitmap->header.cf):0;
    if(!bitmap || bitmap->header.magic!=0x19 || !bpp || !bitmap->header.width || !bitmap->header.height ||
       !bitmap->data || (glyph && bitmap->header.cf!=IMAGE_CF_A8)) {
        error(capture,RESOURCE_ERROR_BITMAP);return false;
    }
    uint32_t width=bitmap->header.width,height=bitmap->header.height,stride=bitmap->header.stride;
    uint32_t row=width*bpp,length=stride*height;
    if(row>stride || length>bitmap->data_size) {error(capture,RESOURCE_ERROR_BITMAP);return false;}
    if(length>capture->byte_capacity-capture->byte_count) {error(capture,RESOURCE_ERROR_CAPACITY);return false;}
    if(glyph) {
        for(uint32_t y=0;y<height;y++) for(uint32_t x=row;x<stride;x++)
            if(bitmap->data[y*stride+x]) {error(capture,RESOURCE_ERROR_PADDING);return false;}
    }
    uint8_t *destination=capture->bytes+capture->byte_count;
    for(uint32_t y=0;y<height;y++) {
        memcpy(destination+y*stride,bitmap->data+y*stride,row);
        memset(destination+y*stride+row,0,stride-row);
        if(bitmap->header.cf==STOCK_CF_XRGB8888)
            for(uint32_t x=0;x<width;x++) destination[y*stride+x*4+3]=0;
    }
    *out=(ResourceBitmap){.offset=capture->byte_count,.length=length,.width=(uint16_t)width,
        .height=(uint16_t)height,.stride=(uint16_t)stride,.flags=bitmap->header.flags,.format=bitmap->header.cf};
    capture->byte_count+=length;
    return true;
}

static void collect_glyph(stock_draw_unit_t *unit,ResourceNativeGlyph *glyph,
                          stock_fill_dsc_t *fill,const stock_area_t *fill_area)
{
    Invocation *call=active;
    ResourceCapture *capture=call->capture;
    capture->native_callbacks++;
    if(unit!=call->unit) error(capture,RESOURCE_ERROR_CONTEXT);
    if(glyph && !capture->errors) {
        bool bitmap_format=glyph->format==1 || glyph->format==2 || glyph->format==4 || glyph->format==8;
        if(!bitmap_format || !area_valid(glyph->letter_coords) || !glyph->metrics) error(capture,RESOURCE_ERROR_UNSUPPORTED);
        else {
            const stock_draw_buf_t *bitmap=glyph->glyph_data;
            if(!bitmap || glyph->metrics->box_width!=bitmap->header.width || glyph->metrics->box_height!=bitmap->header.height)
                error(capture,RESOURCE_ERROR_BITMAP);
            else {
                ResourceEvent *event=event_begin(RESOURCE_GLYPH_A8);
                if(event && save_bitmap(&event->bitmap,bitmap,true)) {
                    event->area=*glyph->letter_coords;event->decoded_clip=event->clip;
                    memcpy(event->data.glyph.color,glyph->color,3);event->data.glyph.opacity=glyph->opacity;
                    event->data.glyph.format=glyph->format;capture->event_count++;
                }
            }
        }
    }
    if((fill || fill_area) && !capture->errors) {
        if(!fill || !area_valid(fill_area) || fill->base.dsc_size!=sizeof(*fill)) error(capture,RESOURCE_ERROR_UNSUPPORTED);
        else {
            bool gradient=false;
            for(size_t i=0;i<sizeof(fill->gradient);i++) gradient|=fill->gradient[i]!=0;
            if(gradient) error(capture,RESOURCE_ERROR_UNSUPPORTED);
            else {
                ResourceEvent *event=event_begin(RESOURCE_FILL);
                if(event) {
                    event->area=*fill_area;event->decoded_clip=event->clip;event->data.fill.radius=fill->radius;
                    event->data.fill.color[0]=fill->blue;event->data.fill.color[1]=fill->green;event->data.fill.color[2]=fill->red;
                    event->data.fill.opacity=fill->opacity;capture->event_count++;
                }
            }
        }
    }
    call->api->glyph(unit,glyph,fill,fill_area);
}

static void collect_image(stock_draw_unit_t *unit,const image_draw_dsc_t *dsc,const image_decoder_t *decoder,
                          image_supplement_t *sup,const stock_area_t *coordinates,const stock_area_t *clip)
{
    Invocation *call=active;ResourceCapture *capture=call->capture;
    capture->native_callbacks++;
    if(unit!=call->unit) error(capture,RESOURCE_ERROR_CONTEXT);
    if(!capture->errors) {
        if(!dsc || !decoder || !decoder->decoded || !sup || !area_valid(coordinates) || !area_valid(clip) ||
           decoder->palette || decoder->palette_size || sup->palette || (sup->palette_size_bits&511) ||
           dsc->bitmap_mask || dsc->supplement || dsc->skew_x || dsc->skew_y || (dsc->flags&15))
            error(capture,RESOURCE_ERROR_UNSUPPORTED);
        else {
            ResourceEvent *event=event_begin(RESOURCE_IMAGE);
            if(event && save_bitmap(&event->bitmap,decoder->decoded,false)) {
                event->area=*coordinates;event->decoded_clip=*clip;
                event->data.image.rotation=dsc->rotation;event->data.image.scale_x=dsc->scale_x;event->data.image.scale_y=dsc->scale_y;
                event->data.image.skew_x=dsc->skew_x;event->data.image.skew_y=dsc->skew_y;
                event->data.image.pivot_x=dsc->pivot_x;event->data.image.pivot_y=dsc->pivot_y;
                event->data.image.image_area=dsc->image_area;event->data.image.clip_radius=dsc->clip_radius;
                memcpy(event->data.image.recolor,dsc->recolor,3);event->data.image.recolor_opacity=dsc->recolor_opacity;
                event->data.image.opacity=dsc->opacity;event->data.image.flags=dsc->flags;
                capture->event_count++;
            }
        }
    }
    call->api->image_core(unit,dsc,decoder,sup,coordinates,clip);
}

static int draw_with_api(ResourceCapture *capture,const NativeApi *api,uint32_t sequence,uint32_t layer_id,
                         stock_draw_unit_t *unit,const stock_task_t *task)
{
    if(!capture) return 0;
    if(!unit || !unit->target_layer || !unit->target_layer->draw_buf || !area_valid(unit->clip_area) ||
       !task || !task->draw_dsc || active || capture->event_count>capture->event_capacity ||
       capture->byte_count>capture->byte_capacity || (capture->event_capacity && !capture->events) ||
       (capture->byte_capacity && !capture->bytes)) {error(capture,RESOURCE_ERROR_CONTEXT);return 0;}
    const stock_draw_dsc_base_t *base=task->draw_dsc;
    if(base->layer!=unit->target_layer ||
       (task->type!=4 && task->type!=5) ||
       (task->type==4 && base->dsc_size!=sizeof(ResourceNativeLabel)) ||
       (task->type==5 && base->dsc_size!=sizeof(image_draw_dsc_t))) {
        error(capture,RESOURCE_ERROR_UNSUPPORTED);return 0;
    }
    Invocation call={.capture=capture,.api=api,.unit=unit,.task_sequence=sequence,.layer_id=layer_id};
    active=&call;
    if(task->type==4) {
        const ResourceNativeLabel *dsc=task->draw_dsc;
        if(dsc->opacity>2) api->label(unit,dsc,&task->area,collect_glyph);
    }
    else {
        const image_draw_dsc_t *dsc=task->draw_dsc;
        (dsc->flags&32?api->tiled:api->image)(unit,dsc,&task->area,collect_image);
    }
    active=NULL;
    return 1;
}
int resource_capture_draw(ResourceCapture *capture,uintptr_t base,uint32_t sequence,uint32_t layer_id,
                          stock_draw_unit_t *unit,const stock_task_t *task)
{
    if(!base) {if(capture) error(capture,RESOURCE_ERROR_CONTEXT);return 0;}
    NativeApi api=native_api(base);
    return draw_with_api(capture,&api,sequence,layer_id,unit,task);
}

static bool restore_bitmap(const ResourceBitmap *asset,const uint8_t *bytes,uint32_t byte_count,stock_draw_buf_t *bitmap)
{
    unsigned bpp=pixel_bytes(asset->format);
    if(!bytes || !bpp || !asset->width || !asset->height || asset->stride<(uint32_t)asset->width*bpp ||
       asset->length!=(uint32_t)asset->stride*asset->height || asset->offset>byte_count ||
       asset->length>byte_count-asset->offset) return false;
    *bitmap=(stock_draw_buf_t){.header={.magic=0x19,.cf=asset->format,.flags=asset->flags,
        .width=asset->width,.height=asset->height,.stride=asset->stride},.data_size=asset->length,
        .data=(uint8_t *)bytes+asset->offset,.unaligned_data=(uint8_t *)bytes+asset->offset};
    return true;
}
static bool replay_with_api(const NativeApi *api,stock_draw_unit_t *unit,const ResourceEvent *event,
                            const uint8_t *bytes,uint32_t byte_count)
{
    if(!unit || !unit->target_layer || !unit->target_layer->draw_buf || !event ||
       !area_valid(&event->area) || !area_valid(&event->clip)) return false;
    stock_draw_buf_t bitmap;
    if(event->kind!=RESOURCE_FILL && !restore_bitmap(&event->bitmap,bytes,byte_count,&bitmap)) return false;
    if(event->kind==RESOURCE_GLYPH_A8 && (event->bitmap.format!=IMAGE_CF_A8 ||
       (event->data.glyph.format!=1 && event->data.glyph.format!=2 && event->data.glyph.format!=4 && event->data.glyph.format!=8))) return false;
    if(event->kind==RESOURCE_IMAGE && (!area_valid(&event->decoded_clip) ||
       event->data.image.skew_x || event->data.image.skew_y || (event->data.image.flags&15) ||
       event->data.image.scale_x<=0 || event->data.image.scale_y<=0)) return false;
    if(event->kind<RESOURCE_GLYPH_A8 || event->kind>RESOURCE_IMAGE) return false;
    const stock_area_t *previous_clip=unit->clip_area;unit->clip_area=&event->clip;
    if(event->kind==RESOURCE_GLYPH_A8) {
        ResourceNativeGlyph glyph={.glyph_data=&bitmap,.format=event->data.glyph.format,
            .letter_coords=&event->area,.opacity=event->data.glyph.opacity};
        memcpy(glyph.color,event->data.glyph.color,3);
        api->glyph(unit,&glyph,NULL,NULL);
    }
    else if(event->kind==RESOURCE_FILL) {
        stock_fill_dsc_t fill;api->fill_init(&fill);fill.base.layer=unit->target_layer;
        fill.radius=event->data.fill.radius;fill.opacity=event->data.fill.opacity;
        fill.blue=event->data.fill.color[0];fill.green=event->data.fill.color[1];fill.red=event->data.fill.color[2];
        api->fill(unit,&fill,&event->area);
    }
    else {
        image_draw_dsc_t dsc={.base={.layer=unit->target_layer,.dsc_size=sizeof(dsc)},.header=bitmap.header,
            .rotation=event->data.image.rotation,.scale_x=event->data.image.scale_x,.scale_y=event->data.image.scale_y,
            .skew_x=event->data.image.skew_x,.skew_y=event->data.image.skew_y,
            .pivot_x=event->data.image.pivot_x,.pivot_y=event->data.image.pivot_y,
            .image_area=event->data.image.image_area,.clip_radius=event->data.image.clip_radius,
            .opacity=event->data.image.opacity,.flags=event->data.image.flags,.recolor_opacity=event->data.image.recolor_opacity};
        memcpy(dsc.recolor,event->data.image.recolor,3);
        image_decoder_t decoder={.decoded=&bitmap};image_supplement_t supplement={0};
        memcpy(supplement.alpha_color,dsc.recolor,3);
        api->image_core(unit,&dsc,&decoder,&supplement,&event->area,&event->decoded_clip);
    }
    unit->clip_area=previous_clip;
    return true;
}
bool resource_replay_event(uintptr_t base,stock_draw_unit_t *unit,const ResourceEvent *event,
                           const uint8_t *bytes,uint32_t byte_count)
{
    if(!base) return false;
    NativeApi api=native_api(base);
    return replay_with_api(&api,unit,event,bytes,byte_count);
}
