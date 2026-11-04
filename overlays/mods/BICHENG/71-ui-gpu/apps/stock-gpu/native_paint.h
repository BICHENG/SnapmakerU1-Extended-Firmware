#ifndef NATIVE_GPU_PAINT_H
#define NATIVE_GPU_PAINT_H
#include "gpu2d.h"
#include "stock_abi.h"
typedef struct {
    stock_draw_dsc_base_t base;
    int32_t radius;
    uint8_t color[3],padding0;
    int32_t width;
    uint8_t opacity,sides,padding1[2];
} NativeBorder;
_Static_assert(sizeof(NativeBorder)==64,"native border descriptor");
_Static_assert(offsetof(NativeBorder,opacity)==60,"native border opacity");
typedef struct {
    stock_draw_dsc_base_t base;
    int32_t radius;
    uint8_t color[3],padding0;
    int32_t width,spread,offset_x,offset_y;
    uint8_t opacity,background_cover,padding1[6];
} NativeShadow;
_Static_assert(sizeof(NativeShadow)==80,"native shadow descriptor");
_Static_assert(offsetof(NativeShadow,opacity)==72,"native shadow opacity");
typedef struct {
    stock_draw_dsc_base_t base;
    float x1,y1,x2,y2;
    uint8_t color[3],padding0;
    int32_t width,dash_width,dash_gap;
    uint8_t opacity,flags,padding1[6];
} NativeLine;
_Static_assert(sizeof(NativeLine)==88,"native line descriptor");
_Static_assert(offsetof(NativeLine,opacity)==80,"native line opacity");
typedef struct {
    stock_draw_dsc_base_t base;
    uint8_t color[3],padding0;
    int32_t width;
    float start,end;
    int32_t x,y;
    uint16_t radius;
    uint8_t padding1[6];
    const void *image;
    uint8_t opacity,rounded,padding2[6];
} NativeArc;
_Static_assert(sizeof(NativeArc)==96,"native arc descriptor");
_Static_assert(offsetof(NativeArc,opacity)==88,"native arc opacity");
typedef struct {
    uintptr_t base;
    UiGpu2D *gpu;
    const char *error;
    unsigned fills,glyphs,images,tasks,borders,shadows,lines,arcs;
    stock_image_header_t rejected_bitmap;
    uint32_t rejected_size;
    bool rejected_has_data;
} NativePaint;
/* The caller owns the native LVGL lock and an idle original software unit. */
bool native_gpu_paint(NativePaint *paint,stock_draw_unit_t *unit,const stock_task_t *task);
#endif
