/**
 * @file include/gdmsp.h
 * @short Daemon Media Player Playback
 * @brief A service that provides media controls in the background of the application.
 */
#ifndef GDMSP_H
#define GDMSP_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    GDMSP_FSM_IDLE = 0,
    GDMSP_FSM_OPENING,    /* service thread executando .source() */
    GDMSP_FSM_LOADING,
    GDMSP_FSM_PLAYING,
    GDMSP_FSM_PAUSED,
    GDMSP_FSM_STOPPING,
    GDMSP_FSM_ERROR,
} gdmsp_fsm_t;

typedef enum {
    GDMSP_CMD_NONE = 0,
    GDMSP_CMD_RESOURCE,
    GDMSP_CMD_PLAY,
    GDMSP_CMD_PAUSE,
    GDMSP_CMD_STOP,
    GDMSP_CMD_TICK,
    GDMSP_CMD_CURRENT_TIME,
    GDMSP_CMD_DURATION,
    GDMSP_CMD_POSITION,
} gdmsp_cmd_t;

typedef union {
    int64_t     i64;
    const char *str;
    struct {
        int16_t x;
        int16_t y;
        int16_t w;
        int16_t h;
    };
} gdmsp_value_t;

typedef struct {
    gdmsp_fsm_t (*src)(uint8_t channel, const char *url, void *usr);
    gdmsp_fsm_t (*set)(uint8_t channel, gdmsp_cmd_t cmd, gdmsp_value_t value, void *usr);
    gdmsp_value_t (*get)(uint8_t channel, gdmsp_cmd_t cmd, void *usr);
} gdmsp_player_t;

typedef struct {
    void        (*source)   (uint8_t channel, const char *url);
    gdmsp_fsm_t (*status)   (uint8_t channel);
    gdmsp_fsm_t (*set)      (uint8_t channel, gdmsp_cmd_t cmd, const gdmsp_value_t *value);
    gdmsp_fsm_t (*get)      (uint8_t channel, gdmsp_cmd_t cmd, gdmsp_value_t *value);
    void        (*position) (uint8_t channel, int16_t x, int16_t y, int16_t w, int16_t h);
    void        (*tick)     (void);
    bool        (*is_active)(void);
} gdmsp_control_t;

const gdmsp_control_t *gdmsp_control(void);

#endif
