#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
typedef struct { float x,y,z; uint8_t color[4]; } Vertex;
typedef struct {
    Vertex *vertices;
    uint64_t *line_end;
    size_t count, capacity;
    size_t *layer_end;
    float *layer_z;
    unsigned layers;
    float min[3],max[3];
    char error[128];
} Model;
int gcode_load(Model *m,const char *path,const atomic_uint *generation,unsigned expected);
void gcode_free(Model *m);
void gcode_progress(const Model *m,uint64_t file_position,size_t *completed,unsigned *layer);
