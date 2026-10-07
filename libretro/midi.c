/* Standard MIDI Files (formats 0/1) scheduled through RETRO_ENVIRONMENT_GET_MIDI_INTERFACE. */
#include "core_internal.h"
#include "audiolib/music.h"
#include <limits.h>
typedef struct {
    uint64_t tick, us;
    unsigned order, size, tempo;
    uint8_t *data;
} midi_event;
static midi_event *events;
static size_t count, capacity, cursor;
static uint64_t position, duration, output_clock, last_write;
static int playing, paused, looping, volume = 200, context;
int MUSIC_ErrorCode;

static unsigned be16(const uint8_t *p) { return (p[0]<<8)|p[1]; }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|(p[2]<<8)|p[3]; }
static int vlq(const uint8_t **p, const uint8_t *end, uint32_t *value)
{
    unsigned i; *value = 0;
    for (i = 0; i < 4 && *p < end; ++i) { uint8_t b = *(*p)++; *value = (*value<<7)|(b&127); if (!(b&128)) return 1; }
    return 0;
}
static int output_enabled(void)
{ return lr_midi.output_enabled && lr_midi.write && lr_midi.output_enabled(); }
static void emit(const uint8_t *data, unsigned size, uint64_t clock)
{
    unsigned i;
    if (!output_enabled()) { last_write = clock; return; }
    for (i = 0; i < size; ++i) {
        uint64_t delta = clock >= last_write ? clock-last_write : 0;
        if (!lr_midi.write(data[i], delta > UINT32_MAX ? UINT32_MAX : (uint32_t)delta)) break;
        last_write = clock;
    }
}
static void silence(uint64_t clock)
{
    unsigned ch;
    for (ch = 0; ch < 16; ++ch) {
        uint8_t sustain[3] = {0xb0|ch, 64, 0}, off[3] = {0xb0|ch, 123, 0};
        emit(sustain, 3, clock); emit(off, 3, clock);
    }
}
void lr_midi_reset(void)
{
    size_t i;
    silence(output_clock);
    for (i = 0; i < count; ++i) free(events[i].data);
    free(events); events = NULL; count = capacity = cursor = 0;
    position = duration = 0; playing = paused = 0;
}
static int add_event(uint64_t tick, const uint8_t *data, unsigned size, unsigned tempo)
{
    midi_event *e;
    if (count == 262144) return 0;
    if (count == capacity) {
        size_t cap = capacity ? capacity*2 : 256;
        midi_event *next = realloc(events, cap*sizeof(*next));
        if (!next) return 0;
        events = next; capacity = cap;
    }
    e = &events[count]; memset(e, 0, sizeof(*e));
    e->tick = tick; e->order = (unsigned)count; e->size = size; e->tempo = tempo;
    if (size) { e->data = malloc(size); if (!e->data) return 0; memcpy(e->data, data, size); }
    ++count; return 1;
}
static int compare(const void *a, const void *b)
{
    const midi_event *x = a, *y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    return x->order < y->order ? -1 : x->order != y->order;
}
static int parse_smf(const uint8_t *data, size_t size)
{
    const uint8_t *p, *end = data + size;
    unsigned tracks, division, t;
    uint64_t endtick = 0, tick = 0, us = 0, remainder = 0;
    uint32_t tempo = 500000;
    size_t i;
    if (size < 14 || memcmp(data,"MThd",4) || be32(data+4)<6 || be32(data+4)>size-8 || be16(data+8)>1) return 0;
    tracks = be16(data+10); division = be16(data+12);
    if (!tracks || !division || division & 0x8000) return 0;
    p = data + 8 + be32(data+4);
    for (t = 0; t < tracks; ++t) {
        const uint8_t *trackend;
        uint64_t tracktick = 0;
        uint8_t running = 0;
        uint32_t len;
        if (end-p < 8 || memcmp(p,"MTrk",4)) return 0;
        len = be32(p+4); p += 8;
        if (len > (size_t)(end-p)) return 0;
        trackend = p+len;
        while (p < trackend) {
            uint32_t delta; unsigned status;
            if (!vlq(&p,trackend,&delta) || p==trackend) return 0;
            tracktick += delta;
            status = *p;
            if (status & 128) { ++p; if (status < 0xf0) running = status; }
            else { if (!running) return 0; status = running; }
            if (status == 0xff) {
                unsigned type;
                if (p == trackend) return 0;
                type = *p++;
                if (!vlq(&p,trackend,&len) || len > (size_t)(trackend-p)) return 0;
                if (type == 0x51 && len == 3) {
                    unsigned nexttempo = (p[0]<<16)|(p[1]<<8)|p[2];
                    if (!nexttempo || !add_event(tracktick,NULL,0,nexttempo)) return 0;
                }
                p += len;
                if (type == 0x2f) break;
            } else if (status == 0xf0 || status == 0xf7) {
                uint8_t *msg;
                if (!vlq(&p,trackend,&len) || len > (size_t)(trackend-p)) return 0;
                msg = malloc(len+1); if (!msg) return 0;
                msg[0] = 0xf0; memcpy(msg+1,p,len);
                /* F7 escape packets already contain their wire representation. */
                if (!add_event(tracktick,status==0xf0 ? msg : msg+1,len+(status==0xf0),0)) { free(msg); return 0; }
                free(msg); p += len; running = 0;
            } else if (status >= 0x80 && status < 0xf0) {
                uint8_t msg[3]; unsigned n = (status&0xe0)==0xc0 ? 1 : 2, j;
                if ((size_t)(trackend-p) < n) return 0;
                msg[0] = status;
                for (j=0;j<n;++j) { if (p[j]&128) return 0; msg[j+1]=p[j]; }
                if (!add_event(tracktick,msg,n+1,0)) return 0;
                p += n;
            } else return 0;
        }
        if (tracktick > endtick) endtick = tracktick;
        p = trackend;
    }
    qsort(events,count,sizeof(*events),compare);
    for (i = 0; i < count; ++i) {
        uint64_t scaled = (events[i].tick-tick)*tempo+remainder;
        us += scaled/division; remainder = scaled%division; tick = events[i].tick;
        events[i].us = us;
        if (events[i].tempo) tempo = events[i].tempo;
    }
    duration = us + ((endtick-tick)*tempo+remainder)/division;
    return count && duration;
}
void lr_midi_advance(unsigned microseconds)
{
    uint64_t frame_start = output_clock;
    output_clock += microseconds;
    if (!playing || paused) return;
    while (microseconds) {
        uint64_t step = duration-position;
        if (step > microseconds) step = microseconds;
        while (cursor < count && events[cursor].us <= position+step) {
            midi_event *e = &events[cursor++];
            if (e->size) emit(e->data,e->size,frame_start+(e->us >= position ? e->us-position : 0));
        }
        position += step; microseconds -= (unsigned)step; frame_start += step;
        if (position == duration) {
            silence(frame_start);
            if (!looping) { playing = 0; break; }
            position = cursor = 0;
        }
    }
}
char *MUSIC_ErrorString(int e) { (void)e; return "Invalid or unsupported MIDI file"; }
int MUSIC_Init(int card,int address) { (void)card;(void)address; output_clock=last_write=0; return MUSIC_Ok; }
int MUSIC_Shutdown(void) { lr_midi_reset(); return MUSIC_Ok; }
int MUSIC_StopSong(void) { lr_midi_reset(); return MUSIC_Ok; }
int MUSIC_PlaySong(char *filename,int loop)
{
    int fd, size; uint8_t *data;
    lr_midi_reset();
    fd = kopen4load(filename,0); if (fd < 0) return MUSIC_Error;
    size = kfilelength(fd);
    if (size < 14 || size > 16*1024*1024) { kclose(fd); return MUSIC_Error; }
    data = malloc(size); if (!data) { kclose(fd); return MUSIC_Error; }
    if (kread(fd,data,size) != size) { free(data); kclose(fd); return MUSIC_Error; }
    kclose(fd);
    if (!parse_smf(data,size)) { free(data); lr_midi_reset(); return MUSIC_Error; }
    free(data); looping = loop; playing = 1; return MUSIC_Ok;
}
void PlayMusic(char *name) { MUSIC_PlaySong(name,MUSIC_LoopSong); }
void MUSIC_SetLoopFlag(int loop) { looping = loop; }
int MUSIC_SongPlaying(void) { return playing; }
void MUSIC_Pause(void) { paused=1; silence(output_clock); }
void MUSIC_Continue(void) { paused=0; }
void MUSIC_SetVolume(int value)
{
    uint8_t msg[] = {0xf0,0x7f,0x7f,0x04,0x01,0,0,0xf7};
    unsigned scaled;
    volume = value < 0 ? 0 : value > 255 ? 255 : value;
    scaled = volume*16383/255; msg[5]=scaled&127;msg[6]=scaled>>7;
    emit(msg,sizeof(msg),output_clock);
}
int MUSIC_GetVolume(void) { return volume; }
void MUSIC_SetMidiChannelVolume(int ch,int value) { uint8_t msg[3]={0xb0|(ch&15),7,value&127};emit(msg,3,output_clock); }
void MUSIC_ResetMidiChannelVolumes(void) { int i;for(i=0;i<16;++i) MUSIC_SetMidiChannelVolume(i,127); }
void MUSIC_SetMaxFMMidiChannel(int ch) { (void)ch; }
void MUSIC_SetContext(int value) { context=value; }
int MUSIC_GetContext(void) { return context; }
void MUSIC_SetSongTime(uint32_t ms) { silence(output_clock);position=(uint64_t)ms*1000;if(position>duration)position=duration;cursor=0;while(cursor<count&&events[cursor].us<position)++cursor; }
void MUSIC_SetSongTick(uint32_t tick) { size_t i;for(i=0;i<count&&events[i].tick<tick;++i);MUSIC_SetSongTime((uint32_t)((i<count?events[i].us:duration)/1000)); }
void MUSIC_SetSongPosition(int measure,int beat,int tick) { (void)measure;(void)beat;(void)tick; }
void MUSIC_GetSongPosition(songposition *p) { memset(p,0,sizeof(*p));p->milliseconds=(uint32_t)(position/1000);if(cursor)p->tickposition=(uint32_t)events[cursor-1].tick; }
void MUSIC_GetSongLength(songposition *p) { memset(p,0,sizeof(*p));p->milliseconds=(uint32_t)(duration/1000);if(count)p->tickposition=(uint32_t)events[count-1].tick; }
int MUSIC_FadeVolume(int to,int ms) { (void)ms;MUSIC_SetVolume(to);return MUSIC_Ok; }
int MUSIC_FadeActive(void) { return 0; }
void MUSIC_StopFade(void) { }
void MUSIC_RerouteMidiChannel(int ch,int (*fn)(int,int,int)) { (void)ch;(void)fn; }
void MUSIC_RegisterTimbreBank(unsigned char *bank) { (void)bank; }
