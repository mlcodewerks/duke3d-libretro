#include "core_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#define chdir _chdir
#endif

retro_environment_t lr_environment;
struct retro_midi_interface lr_midi;
uint64_t lr_frame;
int lr_quit, lr_dead;
char lr_content_path[4096];
static retro_video_refresh_t video;
static retro_audio_sample_t audio;
static retro_audio_sample_batch_t audio_batch;
static retro_input_poll_t input_poll;
static retro_input_state_t input_state;
static retro_log_printf_t logger;
static unsigned controller = RETRO_DEVICE_JOYPAD;
static int loaded, shutdown_sent;
static char content_dir[4096];
extern char lr_save_dir[4096];
static uint32_t framebuffer[LR_WIDTH*LR_HEIGHT];
static int16_t pcm[LR_FRAMES*2];
extern int duke_main(int argc,char **argv);

static void game_entry(void)
{
    char *argv[] = {"chocolate_duke3d",NULL};
    duke_main(1,argv); lr_exit(0);
}
void lr_report_error(const char *message)
{
    struct retro_message notification={message,300};
    if(logger && *message)logger(RETRO_LOG_ERROR,"%s\n",message);
    if(lr_environment && *message)lr_environment(RETRO_ENVIRONMENT_SET_MESSAGE,&notification);
}
static uint32_t little32(const uint8_t *p)
{ return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static bool valid_grp(FILE *file)
{
    uint8_t header[16], entry[16];
    uint32_t files,i; uint64_t used; long size;
    if(fseek(file,0,SEEK_END))return false;size=ftell(file);
    if(size<16 || size>INT32_MAX || fseek(file,0,SEEK_SET))return false;
    if(fread(header,1,16,file)!=16 || memcmp(header,"KenSilverman",12))return false;
    files=little32(header+12);used=16+(uint64_t)files*16;
    if(!files || files>16384 || used>(uint64_t)size)return false;
    for(i=0;i<files;++i) {
        if(fread(entry,1,16,file)!=16)return false;
        used+=little32(entry+12);if(used>(uint64_t)size)return false;
    }
    return true;
}
void lr_exit(int status)
{
    lr_dead = 1;
    if (logger) logger(status ? RETRO_LOG_ERROR : RETRO_LOG_INFO,"Duke3D exited with status %d\n",status);
    for (;;) lr_yield();
}
static void resume(void)
{
    char cwd[4096];
    /* The original engine uses relative filenames. Restore the host directory at every yield. */
    if (!getcwd(cwd,sizeof(cwd)) || chdir(content_dir)) { lr_dead=1;return; }
    lr_context_resume();
    chdir(cwd);
}
void retro_set_environment(retro_environment_t cb)
{
    static const struct retro_input_descriptor inputs[] = {
      {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_UP,"Move forward / Menu up"},
      {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_DOWN,"Move backward / Menu down"},
      {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_LEFT,"Turn left / Menu left"},
      {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT,"Turn right / Menu right"},
      {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A,"Jump / Confirm"},
      {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B,"Crouch"},
      {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X,"Open / Use"},
      {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y,"Use inventory / Confirm"},
      {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L,"Previous weapon"},
      {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R,"Next weapon"},
      {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L2,"Strafe modifier"},
      {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R2,"Fire"},
      {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_START,"Menu / Cancel"},
      {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_SELECT,"Map"},
      {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L3,"Run"},
      {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R3,"Toggle mouse aiming"},
      {0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_LEFT,RETRO_DEVICE_ID_ANALOG_X,"Strafe"},
      {0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_LEFT,RETRO_DEVICE_ID_ANALOG_Y,"Move"},
      {0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_RIGHT,RETRO_DEVICE_ID_ANALOG_X,"Look horizontally"},
      {0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_RIGHT,RETRO_DEVICE_ID_ANALOG_Y,"Look vertically"},
      {0,0,0,0,NULL}
    };
    static const struct retro_controller_description pads[]={{"RetroPad",RETRO_DEVICE_JOYPAD},{"RetroPad with analog",RETRO_DEVICE_ANALOG},{"Keyboard / Mouse",RETRO_DEVICE_MOUSE}};
    static const struct retro_controller_info ports[]={{pads,3},{NULL,0}};
    static const struct retro_variable options[] = {
        {"chocolate_duke3d_autoaim", "Auto aim; Off|Bullet only|Full"},
        {"chocolate_duke3d_crosshair", "Crosshair; On|Off"},
        {NULL, NULL}
    };
    lr_environment=cb;
    if(cb) {
        struct retro_keyboard_callback keyboard={lr_keyboard_event};
        cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS,(void *)inputs);
        cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO,(void *)ports);
        cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK,&keyboard);
        cb(RETRO_ENVIRONMENT_SET_VARIABLES,(void *)options);
    }
}
void retro_set_video_refresh(retro_video_refresh_t cb) { video=cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { audio=cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch=cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll=cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state=cb; }
unsigned retro_api_version(void) { return RETRO_API_VERSION; }
void retro_init(void)
{
    struct retro_log_callback log={0};
    if(lr_environment&&lr_environment(RETRO_ENVIRONMENT_GET_LOG_INTERFACE,&log))logger=log.log;
}
void retro_get_system_info(struct retro_system_info *info)
{ memset(info,0,sizeof(*info));info->library_name="Chocolate Duke3D";info->library_version="1.0-libretro";info->valid_extensions="grp";info->need_fullpath=true;info->block_extract=true; }
void retro_get_system_av_info(struct retro_system_av_info *info)
{ memset(info,0,sizeof(*info));info->geometry.base_width=info->geometry.max_width=LR_WIDTH;info->geometry.base_height=info->geometry.max_height=LR_HEIGHT;info->geometry.aspect_ratio=4.0f/3.0f;info->timing.fps=60.0;info->timing.sample_rate=LR_RATE; }
void retro_set_controller_port_device(unsigned port,unsigned device)
{
    if(port!=0)return;
    controller=device&RETRO_DEVICE_MASK;
    lr_set_input_device(controller);
}
bool retro_load_game(const struct retro_game_info *game)
{
    enum retro_pixel_format format=RETRO_PIXEL_FORMAT_XRGB8888;
    FILE *file; char *slash;
    if(loaded||!game||!game->path||strlen(game->path)>=500||!lr_environment)return false;
    if(!lr_environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT,&format))return false;
    file=fopen(game->path,"rb");if(!file)return false;
    if(!valid_grp(file)) { fclose(file);return false; }
    fclose(file);
    #ifdef _WIN32
    if(!_fullpath(lr_content_path,game->path,sizeof(lr_content_path)))return false;
    #else
    if(!realpath(game->path,lr_content_path))return false;
    #endif
    if(strlen(lr_content_path)>=500)return false;
    strcpy(content_dir,lr_content_path);
    slash=strrchr(content_dir,'/');
    #ifdef _WIN32
    { char *back=strrchr(content_dir,'\\');if(back&&(!slash||back>slash))slash=back; }
    #endif
    if(!slash)return false;slash[1]=0;
    {
        const char *directory=NULL;
        lr_environment(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY,&directory);
        if(!directory || !*directory)directory=content_dir;
        if(strlen(directory)>=sizeof(lr_save_dir))return false;
        strcpy(lr_save_dir,directory);
    }
    memset(&lr_midi,0,sizeof(lr_midi));lr_environment(RETRO_ENVIRONMENT_GET_MIDI_INTERFACE,&lr_midi);
    lr_frame=0;lr_dead=lr_quit=shutdown_sent=0;lr_platform_reset();
    lr_set_input_device(controller);
    if(!lr_context_create(game_entry))return false;
    loaded=1;return true;
}
void retro_run(void)
{
    unsigned i;
    bool updated = false;
    if(!loaded)return;
    if(lr_environment(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE,&updated) && updated)
        lr_apply_options();
    if(input_poll)input_poll();
    lr_input_poll(input_state,controller);
    ++lr_frame;
    if(!lr_dead)resume();
    lr_video_convert(framebuffer);
    if(video)video(framebuffer,LR_WIDTH,LR_HEIGHT,LR_WIDTH*sizeof(uint32_t));
    lr_audio_render(pcm,LR_FRAMES);
    if(audio_batch)audio_batch(pcm,LR_FRAMES);
    else if(audio)for(i=0;i<LR_FRAMES;++i)audio(pcm[i*2],pcm[i*2+1]);
    lr_midi_advance((unsigned)(lr_frame*1000000/60-(lr_frame-1)*1000000/60));
    if(lr_midi.flush)lr_midi.flush();
    if(lr_dead&&!shutdown_sent){shutdown_sent=1;lr_environment(RETRO_ENVIRONMENT_SHUTDOWN,NULL);}
}
void retro_unload_game(void)
{
    unsigned i;
    if(!loaded)return;
    lr_quit=1;
    for(i=0;i<32&&!lr_dead;++i)resume();
    lr_midi_reset();if(lr_midi.flush)lr_midi.flush();lr_context_destroy();loaded=0;
}
void retro_deinit(void) { retro_unload_game();logger=NULL; }
void retro_reset(void)
{
    struct retro_game_info info={0}; char path[4096];
    if(!loaded)return;strcpy(path,lr_content_path);retro_unload_game();info.path=path;retro_load_game(&info);
}
unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void *data,size_t size) { (void)data;(void)size;return false; }
bool retro_unserialize(const void *data,size_t size) { (void)data;(void)size;return false; }
void retro_cheat_reset(void) { }
void retro_cheat_set(unsigned index,bool enabled,const char *code) { (void)index;(void)enabled;(void)code; }
bool retro_load_game_special(unsigned type,const struct retro_game_info *info,size_t num) { (void)type;(void)info;(void)num;return false; }
void *retro_get_memory_data(unsigned id) { (void)id;return NULL; }
size_t retro_get_memory_size(unsigned id) { (void)id;return 0; }
