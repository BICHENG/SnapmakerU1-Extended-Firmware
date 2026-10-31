#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UiGpuOutput UiGpuOutput;

/* All operations run with the creating EGL context current. Attach the returned
 * texture to the caller's FBO and verify completeness before using it.
 * Rows retain GL orientation and RGBA byte order, including alpha. */
UiGpuOutput *ui_gpu_output_create(unsigned width,unsigned height);
unsigned ui_gpu_output_texture(const UiGpuOutput *output);
size_t ui_gpu_output_stride(const UiGpuOutput *output);
const uint8_t *ui_gpu_output_begin_read(UiGpuOutput *output);
int ui_gpu_output_end_read(UiGpuOutput *output);
void ui_gpu_output_destroy(UiGpuOutput *output);

#ifdef __cplusplus
}
#endif
