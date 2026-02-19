#ifndef GEOPENGL_PIPELINE_H
#define GEOPENGL_PIPELINE_H

#include <stdint.h>
#include <stdbool.h>

void ge_pipeline_init(uint16_t w, uint16_t h);
void ge_pipeline_terminate(void);
void ge_pipeline_resize(uint16_t w, uint16_t h);
void ge_pipeline_start(void);
void ge_pipeline_end(void);
void ge_pipeline_flush(void);

#endif
