#include "native_paint.h"
#include <math.h>
#ifdef STOCK_GPU_LAB
#include <stdio.h>
#include <stdlib.h>
#endif

static _Thread_local NativePaint *active;
static UiGpu2DArea area(const stock_area_t *a) { return (UiGpu2DArea){a->x1,a->y1,a->x2,a->y2}; }
static uint32_t rgb(const uint8_t *bgr) { return ((uint32_t)bgr[2]<<16)|((uint32_t)bgr[1]<<8)|bgr[0]; }
static void reject(const char *reason) { if(!active->error)active->error=reason; }
static bool bitmap_valid(const stock_draw_buf_t *b) {
    unsigned bytes=!b?0:b->header.cf==IMAGE_CF_A8?1:b->header.cf==0x12?2:b->header.cf==0x0f?3:
        (b->header.cf==STOCK_CF_ARGB8888||b->header.cf==STOCK_CF_XRGB8888)?4:0;
    return b&&b->data&&b->header.magic==0x19&&bytes&&b->header.width&&b->header.height&&
        b->header.width<=2048&&b->header.height<=2048&&b->header.stride>=b->header.width*bytes&&
        b->data_size>=(uint32_t)b->header.height*b->header.stride;
}
static void fill_gpu(stock_draw_unit_t *unit,const stock_fill_dsc_t *fill,const stock_area_t *coords) {
    if(active->error)return;
    /* Verified in 2.1.0.157 at 0x81118..0x81130: direction is bits 0..2 of descriptor byte 67. */
    if(fill->base.dsc_size!=112||(fill->gradient[11]&7)){reject("fill_gradient_or_abi");return;}
    if(!ui_gpu2d_fill(active->gpu,area(coords),area(unit->clip_area),fill->radius,
        ((uint32_t)fill->red<<16)|((uint32_t)fill->green<<8)|fill->blue,fill->opacity))reject("fill_output");
    ++active->fills;
}
static void glyph_gpu(stock_draw_unit_t *unit,ResourceNativeGlyph *glyph,stock_fill_dsc_t *fill,const stock_area_t *coords) {
    if(active->error)return;
    if(glyph) {
        const stock_draw_buf_t *b=glyph->glyph_data;
        if(!glyph->letter_coords||!bitmap_valid(b)||b->header.cf!=IMAGE_CF_A8){reject("glyph_bitmap");return;}
        if(!ui_gpu2d_bitmap(active->gpu,area(glyph->letter_coords),area(unit->clip_area),b->data,
            b->header.width,b->header.height,b->header.stride,b->header.cf,rgb(glyph->color),glyph->opacity,NULL))reject("glyph_output");
        ++active->glyphs;
    }
    if(fill)fill_gpu(unit,fill,coords);
}
static void image_gpu(stock_draw_unit_t *unit,const image_draw_dsc_t *dsc,const image_decoder_t *decoder,
                      image_supplement_t *sup,const stock_area_t *coords,const stock_area_t *clip) {
    (void)unit;(void)sup;
    if(active->error)return;
    const stock_draw_buf_t *b=decoder?decoder->decoded:NULL;
    if(!bitmap_valid(b)){
        if(b){active->rejected_bitmap=b->header;active->rejected_size=b->data_size;active->rejected_has_data=b->data!=NULL;}
        reject("image_bitmap");return;
    }
    if(dsc->scale_x<=0||dsc->scale_y<=0||dsc->scale_x>16384||dsc->scale_y>16384||dsc->skew_x||dsc->skew_y||
        dsc->bitmap_mask||dsc->clip_radius||(dsc->flags&15)||
        (b->header.cf!=IMAGE_CF_A8&&dsc->recolor_opacity)) {
#ifdef STOCK_GPU_LAB
        const char *dir=getenv("NATIVE_GPU_PROBE_DIR");char path[512];
        if(dir){snprintf(path,sizeof(path),"%s/image.jsonl",dir);FILE *f=fopen(path,"a");
            if(f){fprintf(f,"{\"rotation\":%d,\"scale\":[%d,%d],\"skew\":[%d,%d],\"mask\":%s,\"clip_radius\":%d,\"flags\":%u,\"recolor\":%u}\n",
                dsc->rotation,dsc->scale_x,dsc->scale_y,dsc->skew_x,dsc->skew_y,dsc->bitmap_mask?"true":"false",dsc->clip_radius,dsc->flags,dsc->recolor_opacity);fclose(f);}}
#endif
        reject("image_transform_or_blend");return;
    }
    float matrix[6],*transform=NULL;
    if(dsc->rotation||dsc->scale_x!=256||dsc->scale_y!=256) {
        float angle=dsc->rotation*(3.14159265358979323846f/1800.f);
        float c=cosf(angle),s=sinf(angle),sx=dsc->scale_x/256.f,sy=dsc->scale_y/256.f;
        float px=coords->x1+dsc->pivot_x,py=coords->y1+dsc->pivot_y;
        matrix[0]=sx*c;matrix[1]=sx*s;matrix[2]=-sy*s;matrix[3]=sy*c;
        matrix[4]=px-px*matrix[0]-py*matrix[2];matrix[5]=py-px*matrix[1]-py*matrix[3];
        transform=matrix;
    }
    if(!ui_gpu2d_bitmap(active->gpu,area(coords),area(clip),b->data,b->header.width,b->header.height,b->header.stride,
        b->header.cf,rgb(dsc->recolor),dsc->opacity,transform))reject("image_output");
    ++active->images;
}
bool native_gpu_paint(NativePaint *paint,stock_draw_unit_t *unit,const stock_task_t *task) {
    if(!paint||paint->error||active||!unit||!unit->clip_area||!unit->target_layer||!task||!task->draw_dsc)return false;
    const stock_draw_dsc_base_t *base=task->draw_dsc;
    if(base->layer!=unit->target_layer||base->dsc_size!=(task->type==1?112u:task->type==2?64u:task->type==3?80u:task->type==4?128u:task->type==5||task->type==6?144u:task->type==7?88u:task->type==8?96u:0u)) {
        paint->error="task_or_descriptor";return false;
    }
    active=paint;
    if(task->type==1)fill_gpu(unit,task->draw_dsc,&task->area);
    else if(task->type==2) {
        const NativeBorder *b=task->draw_dsc;
        if(!ui_gpu2d_border(paint->gpu,area(&task->area),area(unit->clip_area),b->radius,b->width,
            b->sides&31,rgb(b->color),b->opacity))reject("border_geometry");
        ++paint->borders;
    }
    else if(task->type==4) {
        ((ResourceLabelIterator)(paint->base+0x85f90u))(unit,task->draw_dsc,&task->area,glyph_gpu);
    }
    else if(task->type==3) {
        const NativeShadow *s=task->draw_dsc;
        if(!ui_gpu2d_shadow(paint->gpu,area(&task->area),area(unit->clip_area),s->radius,s->width,
            s->spread,s->offset_x,s->offset_y,rgb(s->color),s->opacity))reject("shadow_geometry");
        ++paint->shadows;
    }
    else if(task->type==7) {
        const NativeLine *l=task->draw_dsc;
        if((l->flags&3)||l->dash_width||l->dash_gap||
           !ui_gpu2d_line(paint->gpu,area(unit->clip_area),l->x1,l->y1,l->x2,l->y2,
             l->width,(l->flags>>2)&3,rgb(l->color),l->opacity))reject("line_geometry");
        ++paint->lines;
    }
    else if(task->type==8) {
        const NativeArc *a=task->draw_dsc;
        if(a->image||!ui_gpu2d_arc(paint->gpu,area(unit->clip_area),a->x,a->y,a->radius,
            a->width,a->start,a->end,a->rounded&1,rgb(a->color),a->opacity))reject("arc_geometry");
        ++paint->arcs;
    }
    else if(task->type==6) {
        const image_draw_dsc_t *d=task->draw_dsc;
        if(d->rotation||d->scale_x!=256||d->scale_y!=256||d->skew_x||d->skew_y||d->bitmap_mask||
           d->clip_radius||(d->flags&15)||d->recolor_opacity||
           !ui_gpu2d_layer(paint->gpu,d->src,area(&task->area),area(unit->clip_area),d->opacity))reject("layer_composite");
        ++paint->images;
    }
    else {
        const image_draw_dsc_t *dsc=task->draw_dsc;
        ((image_helper_fn)(paint->base+(dsc->flags&32?IMAGE_TILED_OFFSET:IMAGE_NORMAL_OFFSET)))(unit,dsc,&task->area,image_gpu);
    }
    active=NULL;
    ++paint->tasks;
    return !paint->error;
}
