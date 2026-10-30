#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
typedef struct { float x,y,z; uint8_t color[4]; } Vertex;
typedef struct {
    Vertex *vertices;
    size_t count, capacity;
    size_t *layer_end;
    float *layer_z;
    unsigned layers;
    float min[3],max[3];
    char error[128];
} Model;
int gcode_load(Model *m,const char *path,const atomic_uint *generation,unsigned expected);
void gcode_free(Model *m);
