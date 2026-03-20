#ifndef GEMETRICS_H
#define GEMETRICS_H

#include <stdint.h>
#include "gecnd.h"

#define GECND_METRICS_PRINT (1u << 0) // 1
#define GECND_METRICS_DRAW  (1u << 1) // 2

void gecnd_metrics_start_input(void);
void gecnd_metrics_finish_input(void);

void gecnd_metrics_start_loop(void);
void gecnd_metrics_finish_loop(void);

void gecnd_metrics_start_draw(void);
void gecnd_metrics_finish_draw(void);

void gecnd_metrics_start_post(void);
void gecnd_metrics_finish_post(void);

void gecnd_metrics_start_tint(void);
void gecnd_metrics_finish_tint(void);

void gecnd_metrics_start_wait(void);
void gecnd_metrics_finish_wait(void);

void gecnd_metrics_update(void);
void gecnd_metrics_render(gecnd_t *gly);
void gecnd_metrics_print(void);

void gecnd_metrics_setup(uint32_t flags);
uint32_t gecnd_metrics_get_flags(void);

uint64_t gecnd_metrics_get_lua_peak(void);
uint64_t gecnd_metrics_get_lua_avg(void);

uint32_t gecnd_metrics_get_input_time(void);
uint32_t gecnd_metrics_get_input_worst(void);
uint32_t gecnd_metrics_get_loop_time(void);
uint32_t gecnd_metrics_get_draw_time(void);
uint32_t gecnd_metrics_get_post_time(void);
uint32_t gecnd_metrics_get_tint_time(void);
uint32_t gecnd_metrics_get_wait_time(void);

uint16_t gecnd_metrics_get_fps_avg(void);
uint16_t gecnd_metrics_get_fps_immediate(void);
uint16_t gecnd_metrics_get_fps_worst(void);
uint16_t gecnd_metrics_get_fps_percent(void);
uint16_t gecnd_metrics_get_fps_drops(void);

int32_t     gecnd_profile_get_temp(void);
const char *gecnd_profile_get_local_ip(void);
void        gecnd_profile_ip_refresh(void);

#endif
