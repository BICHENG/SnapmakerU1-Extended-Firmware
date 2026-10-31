#pragma once
#include "gcode.h"
#define WIDTH 480
#define HEIGHT 320
typedef struct Gpu Gpu;
typedef struct {
    unsigned layer;
    int flat,width,height,compact;
    float yaw,tilt,zoom;
    size_t completed;
} RenderView;
Gpu *gpu_create(const Model *model,char *error,size_t size);
int gpu_render(Gpu *g,const Model *m,const RenderView *view,uint8_t *pixels);
void gpu_destroy(Gpu *g);
