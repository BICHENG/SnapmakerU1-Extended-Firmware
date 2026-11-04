#ifndef UI_GPU2D_H
#define UI_GPU2D_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct UiGpu2D UiGpu2D;
typedef struct { int x1,y1,x2,y2; } UiGpu2DArea;
UiGpu2D *ui_gpu2d_create(int width,int height);
void ui_gpu2d_destroy(UiGpu2D *gpu);
bool ui_gpu2d_begin(UiGpu2D *gpu);
bool ui_gpu2d_target(UiGpu2D *gpu,const void *layer,UiGpu2DArea area);
bool ui_gpu2d_layer(UiGpu2D *gpu,const void *layer,UiGpu2DArea area,UiGpu2DArea clip,unsigned opacity);
bool ui_gpu2d_fill(UiGpu2D *gpu,UiGpu2DArea area,UiGpu2DArea clip,int radius,uint32_t rgb,unsigned opacity);
bool ui_gpu2d_border(UiGpu2D *gpu,UiGpu2DArea area,UiGpu2DArea clip,int radius,int width,unsigned sides,uint32_t rgb,unsigned opacity);
bool ui_gpu2d_shadow(UiGpu2D *gpu,UiGpu2DArea area,UiGpu2DArea clip,int radius,int width,int spread,int dx,int dy,uint32_t rgb,unsigned opacity);
bool ui_gpu2d_line(UiGpu2D *gpu,UiGpu2DArea clip,float x1,float y1,float x2,float y2,int width,unsigned caps,uint32_t rgb,unsigned opacity);
bool ui_gpu2d_arc(UiGpu2D *gpu,UiGpu2DArea clip,int x,int y,int radius,int width,float start,float end,bool rounded,uint32_t rgb,unsigned opacity);
bool ui_gpu2d_bitmap(UiGpu2D *gpu,UiGpu2DArea area,UiGpu2DArea clip,
    const uint8_t *pixels,int width,int height,int stride,unsigned format,uint32_t rgb,unsigned opacity,const float matrix[6]);
const uint8_t *ui_gpu2d_finish(UiGpu2D *gpu);
bool ui_gpu2d_read_end(UiGpu2D *gpu);
#endif
