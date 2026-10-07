#ifndef DUKE_RETRO_INTERNAL_H
#define DUKE_RETRO_INTERNAL_H
#include "libretro.h"
#define LR_WIDTH 640
#define LR_HEIGHT 480
#define LR_RATE 44100
#define LR_FRAMES (LR_RATE / 60)
extern retro_environment_t lr_environment;
extern struct retro_midi_interface lr_midi;
extern uint64_t lr_frame;
extern int lr_quit, lr_dead;
extern char lr_content_path[4096];
void lr_yield(void);
void lr_exit(int status);
int lr_context_create(void (*entry)(void));
void lr_context_resume(void);
void lr_context_destroy(void);
void lr_input_poll(retro_input_state_t state, unsigned device);
void lr_set_input_device(unsigned device);
void lr_keyboard_event(bool down, unsigned key, uint32_t character, uint16_t modifiers);
int lr_pad_action(int function);
void lr_platform_reset(void);
void lr_video_convert(uint32_t *out);
void lr_audio_render(int16_t *out, unsigned frames);
void lr_midi_advance(unsigned microseconds);
void lr_midi_reset(void);
void lr_game_config(void);
void lr_apply_options(void);
void lr_report_error(const char *message);
#endif
