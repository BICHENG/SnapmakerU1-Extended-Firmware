#include "native_dispatch.h"
#include <string.h>

bool native_gpu_dispatch_init(NativeDispatch *a,uintptr_t base,UiGpu2D *gpu,
    stock_draw_sw_unit_t *unit,stock_layer_t *root,bool shadow) {
    if(!a||!base||!gpu||!unit||!root||unit->base.next||
       (uintptr_t)unit->base.dispatch_cb!=base+STOCK_SW_DISPATCH_OFFSET||
       !__atomic_load_n(&unit->initialized,__ATOMIC_ACQUIRE)||
       __atomic_load_n(&unit->task_active,__ATOMIC_ACQUIRE))return false;
    *a=(NativeDispatch){.paint={.base=base,.gpu=gpu},.unit=unit,.root=root,
        .original=unit->base.dispatch_cb,.shadow=shadow};
    return true;
}
static int32_t delegate(NativeDispatch *a,stock_draw_unit_t *unit,stock_layer_t *layer) {
    int32_t result=a->original(unit,layer);
    if(result==1)++a->cpu_tasks;
    return result;
}
static void reject_task(NativeDispatch *a,const stock_task_t *task) {
    ++a->unsupported;
    if(!a->first_error)a->first_error=a->paint.error;
    if(a->rejected_count<16&&task&&task->draw_dsc) {
        const stock_draw_dsc_base_t *dsc=task->draw_dsc;
        unsigned size=dsc->dsc_size;
        if(size>160)size=160;
        unsigned i=a->rejected_count++;
        a->rejected[i].type=task->type;a->rejected[i].size=size;
        memcpy(a->rejected[i].data,dsc,size);
    }
}
int32_t native_gpu_dispatch(NativeDispatch *a,stock_draw_unit_t *unit,stock_layer_t *layer) {
    if(!a||!a->original||unit!=&a->unit->base||!layer)return -1;
    if(a->shadow&&a->paint.error)a->paint.error=NULL;
    if(!a->enabled||a->paint.error)return delegate(a,unit,layer);
    if(__atomic_load_n(&a->unit->task_active,__ATOMIC_ACQUIRE))return 0;
    stock_task_t *(*next)(stock_layer_t *,stock_task_t *,uint8_t)=
        (void *)(a->paint.base+STOCK_GET_TASK_OFFSET);
    stock_task_t *task=next(layer,NULL,1);
    if(!task)return -1;
    if(task->state!=1||task->preferred_unit!=1||
       (layer->color_format!=STOCK_CF_XRGB8888&&layer->color_format!=STOCK_CF_ARGB8888&&layer->color_format!=0x0f)) {
        a->paint.error="unsupported_layer_or_queue";reject_task(a,task);
        return delegate(a,unit,layer);
    }
    void *(*allocate)(stock_layer_t *)=(void *)(a->paint.base+STOCK_LAYER_ALLOC_OFFSET);
    if(!allocate(layer)){a->paint.error="layer_buffer";return delegate(a,unit,layer);}
    stock_area_t b=layer->buf_area;
    if(!ui_gpu2d_target(a->paint.gpu,layer==a->root?NULL:layer,(UiGpu2DArea){b.x1,b.y1,b.x2,b.y2})) {
        a->paint.error="gpu_layer_buffer";reject_task(a,task);return delegate(a,unit,layer);
    }
    stock_layer_t *saved_layer=unit->target_layer;
    const stock_area_t *saved_clip=unit->clip_area;
    unit->target_layer=layer;unit->clip_area=&task->clip_area;
    bool painted=native_gpu_paint(&a->paint,unit,task);
    unit->target_layer=saved_layer;unit->clip_area=saved_clip;
    if(!painted) {reject_task(a,task);return delegate(a,unit,layer);}
    ++a->gpu_tasks;
    if(a->shadow)return delegate(a,unit,layer);
    /* Native dispatch_layer retains ownership of descriptors and frees READY tasks. */
    __atomic_store_n(&task->state,3,__ATOMIC_RELEASE);
    ((void (*)(void))(a->paint.base+STOCK_DISPATCH_REQUEST_OFFSET))();
    return 1;
}
