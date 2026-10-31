#ifndef COMPOSE_GPU_H
#define COMPOSE_GPU_H
#include "../abi/stock_draw_abi.h"
#include <stdio.h>
enum { COMPOSE_W=480, COMPOSE_H=320, COMPOSE_STRIDE=1920, COMPOSE_BYTES=1920*320 };
typedef struct ComposeGpu ComposeGpu;
typedef struct {
    stock_area_t area, clip;
    uint8_t red, green, blue, opacity;
    const uint8_t *mask;
    unsigned mask_width, mask_height, mask_stride;
} ComposeCommand;
typedef struct { double submit_us, finish_us, read_copy_us, total_us; } ComposeTiming;
ComposeGpu *compose_create(char *error,size_t capacity);
void compose_destroy(ComposeGpu *gpu);
void compose_capabilities(ComposeGpu *gpu,FILE *report);
int compose_has_fetch(const ComposeGpu *gpu);
int compose_prepare(ComposeGpu *gpu,const ComposeCommand *commands,unsigned count);
int compose_render(ComposeGpu *gpu,int fetch,uint8_t *pixels,ComposeTiming *timing);
uint8_t *compose_cpu_begin(ComposeGpu *gpu);
int compose_cpu_end(ComposeGpu *gpu);
#endif
