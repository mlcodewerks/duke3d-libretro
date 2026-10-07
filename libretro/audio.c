/* Pull the original MultiVoc mixer into libretro's signed stereo PCM callback. */
#include <stdint.h>
#include <string.h>
#include "audiolib/dsl.h"
#include "core_internal.h"
extern int MV_MixPage;
static char *buffer;
static unsigned division_bytes, offset, rate, mode;
static void (*mix_callback)(void);
char *DSL_ErrorString(int error) { (void)error; return "libretro PCM driver"; }
int DSL_Init(void) { return DSL_Ok; }
void DSL_StopPlayback(void) { mix_callback = NULL; buffer = NULL; offset = division_bytes = 0; }
void DSL_Shutdown(void) { DSL_StopPlayback(); }
unsigned DSL_GetPlaybackRate(void) { return rate; }
int DSL_BeginBufferedPlayback(char *start, int bytes, int divisions, unsigned sample_rate, int mix_mode, void (*callback)(void))
{
    unsigned frame_bytes = ((mix_mode & STEREO) ? 2 : 1) * ((mix_mode & SIXTEEN_BIT) ? 2 : 1);
    if (!start || !callback || bytes <= 0 || divisions <= 0 || bytes % divisions ||
        (bytes / divisions) % frame_bytes || sample_rate != LR_RATE) return DSL_Error;
    buffer = start; division_bytes = bytes / divisions; offset = division_bytes;
    rate = sample_rate; mode = mix_mode; mix_callback = callback;
    return DSL_Ok;
}
void lr_audio_render(int16_t *out, unsigned frames)
{
    unsigned i;
    memset(out, 0, frames * 2 * sizeof(*out));
    if (!mix_callback) return;
    for (i = 0; i < frames; ++i) {
        const uint8_t *p;
        unsigned channels = (mode & STEREO) ? 2 : 1, bytes = (mode & SIXTEEN_BIT) ? 2 : 1, c;
        if (offset >= division_bytes) { mix_callback(); offset = 0; }
        p = (const uint8_t *)buffer + MV_MixPage * division_bytes + offset;
        for (c = 0; c < 2; ++c) {
            unsigned k = (channels == 2 ? c : 0) * bytes;
            out[i*2+c] = bytes == 2 ? (int16_t)(p[k] | (p[k+1] << 8)) : ((int)p[k] - 128) * 256;
        }
        offset += channels * bytes;
    }
}
uint32_t DisableInterrupts(void) { return 0; }
void RestoreInterrupts(uint32_t flags) { (void)flags; }
