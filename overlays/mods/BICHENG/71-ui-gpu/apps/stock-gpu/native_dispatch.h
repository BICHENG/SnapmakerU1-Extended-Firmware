#ifndef NATIVE_GPU_DISPATCH_H
#define NATIVE_GPU_DISPATCH_H
#include "native_paint.h"
typedef struct {
    NativePaint paint;
    stock_draw_sw_unit_t *unit;
    stock_layer_t *root;
    int32_t (*original)(stock_draw_unit_t *,stock_layer_t *);
    bool shadow,enabled;
    unsigned gpu_tasks,cpu_tasks,unsupported;
    const char *first_error;
    struct {unsigned type,size;uint8_t data[160];} rejected[16];
    unsigned rejected_count;
} NativeDispatch;
bool native_gpu_dispatch_init(NativeDispatch *adapter,uintptr_t base,UiGpu2D *gpu,
    stock_draw_sw_unit_t *unit,stock_layer_t *root,bool shadow);
int32_t native_gpu_dispatch(NativeDispatch *adapter,stock_draw_unit_t *unit,stock_layer_t *layer);
#endif
