
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool jack_wave1_audio_begin(unsigned sample_rate);
size_t jack_wave1_audio_submit(const int16_t *samples, size_t frames);
void jack_wave1_audio_end_frame(void);
bool jack_wave1_audio_wait(void);
void jack_wave1_audio_mute(bool mute);
void jack_wave1_audio_report(void);
#ifdef JACK_MEGADUCK_AUDIO044
void jack_wave1_audio044_set_enabled(bool enabled);
bool jack_wave1_audio044_get_enabled(void);
#endif
#ifdef __cplusplus
}
#endif
