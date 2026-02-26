#ifndef LIBRETRO_H__
#define LIBRETRO_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define RETRO_API_VERSION 1

#define RETRO_DEVICE_NONE         0
#define RETRO_DEVICE_JOYPAD       1

#define RETRO_DEVICE_ID_JOYPAD_B        0
#define RETRO_DEVICE_ID_JOYPAD_Y        1
#define RETRO_DEVICE_ID_JOYPAD_SELECT   2
#define RETRO_DEVICE_ID_JOYPAD_START    3
#define RETRO_DEVICE_ID_JOYPAD_UP       4
#define RETRO_DEVICE_ID_JOYPAD_DOWN     5
#define RETRO_DEVICE_ID_JOYPAD_LEFT     6
#define RETRO_DEVICE_ID_JOYPAD_RIGHT    7
#define RETRO_DEVICE_ID_JOYPAD_A        8
#define RETRO_DEVICE_ID_JOYPAD_X        9
#define RETRO_DEVICE_ID_JOYPAD_L       10
#define RETRO_DEVICE_ID_JOYPAD_R       11
#define RETRO_DEVICE_ID_JOYPAD_L2      12
#define RETRO_DEVICE_ID_JOYPAD_R2      13
#define RETRO_DEVICE_ID_JOYPAD_L3      14
#define RETRO_DEVICE_ID_JOYPAD_R3      15

#define RETRO_REGION_NTSC  0
#define RETRO_REGION_PAL   1

#define RETRO_PIXEL_FORMAT_0RGB1555 0
#define RETRO_PIXEL_FORMAT_XRGB8888 1
#define RETRO_PIXEL_FORMAT_RGB565   2

#define RETRO_ENVIRONMENT_SET_ROTATION  1
#define RETRO_ENVIRONMENT_GET_CAN_DUPE   2
#define RETRO_ENVIRONMENT_GET_VARIABLE   4
#define RETRO_ENVIRONMENT_SET_VARIABLES  5
#define RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE 6
#define RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME 7
#define RETRO_ENVIRONMENT_GET_LIBRETRO_PATH 8
#define RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK 9
#define RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK 10
#define RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE 11
#define RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES 12
#define RETRO_ENVIRONMENT_GET_SENSOR_INTERFACE 13
#define RETRO_ENVIRONMENT_GET_CAMERA_INTERFACE 14
#define RETRO_ENVIRONMENT_GET_LOG_INTERFACE 15
#define RETRO_ENVIRONMENT_GET_PERF_INTERFACE 16
#define RETRO_ENVIRONMENT_GET_LOCATION_INTERFACE 17
#define RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY 18
#define RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY 19
#define RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO 20
#define RETRO_ENVIRONMENT_SET_PROC_ADDRESS_CALLBACK 21
#define RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO 22
#define RETRO_ENVIRONMENT_SET_CONTROLLER_INFO 23
#define RETRO_ENVIRONMENT_SET_MEMORY_MAPS 24
#define RETRO_ENVIRONMENT_SET_GEOMETRY 25
#define RETRO_ENVIRONMENT_GET_USERNAME 26
#define RETRO_ENVIRONMENT_GET_LANGUAGE 27
#define RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER 28
#define RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE 29
#define RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS 30
#define RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE 31
#define RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS 32
#define RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT 33
#define RETRO_ENVIRONMENT_GET_VFS_INTERFACE 34
#define RETRO_ENVIRONMENT_GET_LED_INTERFACE 35
#define RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE 36
#define RETRO_ENVIRONMENT_GET_MIDI_INTERFACE 37
#define RETRO_ENVIRONMENT_GET_FASTFORWARDING 38
#define RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE 39
#define RETRO_ENVIRONMENT_GET_INPUT_BITMASKS 40
#define RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION 41
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS 42
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL 43
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY 44
#define RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION 45
#define RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE 46
#define RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION 47
#define RETRO_ENVIRONMENT_SET_MESSAGE_INTERFACE 48
#define RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS 49
#define RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK 50
#define RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY 51
#define RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE 52
#define RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE 53
#define RETRO_ENVIRONMENT_GET_GAME_INFO_EXT 54
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2 55
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL 56
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK 57
#define RETRO_ENVIRONMENT_SET_VARIABLE 58
#define RETRO_ENVIRONMENT_GET_THROTTLE_STATE 59
#define RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT 60
#define RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT 61
#define RETRO_ENVIRONMENT_GET_JIT_CAPABLE 62
#define RETRO_ENVIRONMENT_GET_MICROPHONE_INTERFACE 63
#define RETRO_ENVIRONMENT_GET_DEVICE_POWER 64
#define RETRO_ENVIRONMENT_SET_PIXEL_FORMAT 10
#define RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY 9

#define RETRO_LOG_DEBUG 0
#define RETRO_LOG_INFO  1
#define RETRO_LOG_WARN  2
#define RETRO_LOG_ERROR 3

typedef void (*retro_log_printf_t)(int level, const char *fmt, ...);
struct retro_log_callback {
   retro_log_printf_t log;
};

struct retro_message
{
   const char *msg;
   unsigned frames;
};

struct retro_system_info
{
   const char *library_name;
   const char *library_version;
   const char *valid_extensions;
   bool        need_fullpath;
   bool        block_extract;
};

struct retro_game_geometry
{
   unsigned base_width;
   unsigned base_height;
   unsigned max_width;
   unsigned max_height;
   float    aspect_ratio;
};

struct retro_system_timing
{
   double fps;
   double sample_rate;
};

struct retro_system_av_info
{
   struct retro_game_geometry geometry;
   struct retro_system_timing timing;
};

struct retro_variable
{
   const char *key;
   const char *value;
};

struct retro_game_info
{
   const char *path;
   const void *data;
   size_t      size;
   const char *meta;
};

typedef void (*retro_video_refresh_t)(const void *data, unsigned width, unsigned height, size_t pitch);
typedef void (*retro_audio_sample_t)(int16_t left, int16_t right);
typedef size_t (*retro_audio_sample_batch_t)(const int16_t *data, size_t frames);
typedef void (*retro_input_poll_t)(void);
typedef int16_t (*retro_input_state_t)(unsigned port, unsigned device, unsigned index, unsigned id);

typedef bool (*retro_environment_t)(unsigned cmd, void *data);

void retro_set_environment(retro_environment_t);
void retro_set_video_refresh(retro_video_refresh_t);
void retro_set_audio_sample(retro_audio_sample_t);
void retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void retro_set_input_poll(retro_input_poll_t);
void retro_set_input_state(retro_input_state_t);

void retro_init(void);
void retro_deinit(void);
unsigned retro_api_version(void);
void retro_get_system_info(struct retro_system_info *info);
void retro_get_system_av_info(struct retro_system_av_info *info);
void retro_set_controller_port_device(unsigned port, unsigned device);
void retro_reset(void);
void retro_run(void);
size_t retro_serialize_size(void);
bool retro_serialize(void *data, size_t size);
bool retro_unserialize(const void *data, size_t size);
void retro_cheat_reset(void);
void retro_cheat_set(unsigned index, bool enabled, const char *code);
bool retro_load_game(const struct retro_game_info *game);
bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info);
void retro_unload_game(void);
unsigned retro_get_region(void);
void *retro_get_memory_data(unsigned id);
size_t retro_get_memory_size(unsigned id);

#endif
