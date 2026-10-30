#pragma once
#include "gcode.h"
#define WIDTH 480
#define HEIGHT 320
typedef struct Gpu Gpu;
Gpu *gpu_create(const Model *model,char *error,size_t size);
int gpu_render(Gpu *g,const Model *m,unsigned layer,int flat,float yaw,float tilt,float zoom,uint8_t *pixels);
void gpu_destroy(Gpu *g);
