/* Libretro software display, input and deterministic 120 Hz engine timer. */
#include "core_internal.h"
#include "duke3d.h"
#include "display.h"
#include "engine.h"
#include "draw.h"
#include "SDL.h"

int32_t xres, yres, bytesperline, imageSize, maxpages;
uint8_t *frameplace, *frameoffset, *screen, vesachecked;
int32_t buffermode, origbuffermode, linearmode;
uint8_t permanentupdate, vgacompatible;
uint8_t lastPalette[768];
int32_t total_render_time = 1, total_rendered_frames;
static uint8_t pixels[LR_WIDTH * LR_HEIGHT], dac[1024];
static uint32_t colors[256];
static unsigned scancodes[RETROK_LAST];
static uint8_t previous[RETROK_LAST], lastkey, drawcolor;
static uint8_t callback_keys[RETROK_LAST], pad_actions[NUMGAMEFUNCTIONS];
static uint8_t pad_consumed[NUMGAMEFUNCTIONS];
static unsigned input_device=RETRO_DEVICE_JOYPAD;
static int mouse_x, mouse_y, mouse_buttons, timer_rate;
static uint64_t timer_sample;
static unsigned event_budget;
static void (*timer_callback)(void);
SDL_Surface *surface;

static void sendkey(unsigned code, int down)
{
    if (code & 0xff00) { lastkey = 0xe0; keyhandler(); }
    lastkey = (code & 0x7f) | (down ? 0 : 0x80);
    keyhandler();
}

void lr_platform_reset(void)
{
    memset(pixels, 0, sizeof(pixels));
    memset(dac, 0, sizeof(dac)); memset(colors, 0, sizeof(colors));
    memset(previous, 0, sizeof(previous)); memset(scancodes, 0, sizeof(scancodes));
    memset(callback_keys, 0, sizeof(callback_keys)); memset(pad_actions, 0, sizeof(pad_actions));
    memset(pad_consumed, 0, sizeof(pad_consumed));
    mouse_x = mouse_y = mouse_buttons = timer_rate = 0;
    event_budget = 0; timer_sample = 0; timer_callback = NULL;
    frameplace = frameoffset = pixels; xres = LR_WIDTH; yres = LR_HEIGHT;
    #include "keymap.inc"
}

static int pad(retro_input_state_t state, unsigned id)
{ return state(0, RETRO_DEVICE_JOYPAD, 0, id) != 0; }

static void enable_mouselook(void)
{
    /* The game uses MouseAiming=1 for held aiming, not always-on aiming. */
    MouseAiming = 0;
    myaimmode = 1;
    myaimstat = omyaimstat = 0;
    ps[myconnectindex].aim_mode = 1;
}

void lr_set_input_device(unsigned device)
{
    unsigned i;
    if(device==input_device)return;
    /* Release held controls and discard pending motion before changing sources. */
    for(i=0;i<RETROK_LAST;++i)
        if(previous[i] && scancodes[i])sendkey(scancodes[i],0);
    memset(previous,0,sizeof(previous));
    memset(pad_actions,0,sizeof(pad_actions));
    memset(pad_consumed,0,sizeof(pad_consumed));
    mouse_x=mouse_y=mouse_buttons=0;
    KB_ClearKeysDown();
    input_device=device;
    if(device==RETRO_DEVICE_MOUSE || device==RETRO_DEVICE_KEYBOARD ||
       device==RETRO_DEVICE_JOYPAD || device==RETRO_DEVICE_ANALOG)
        enable_mouselook();
}

void lr_keyboard_event(bool down, unsigned key, uint32_t character, uint16_t modifiers)
{ (void)character; (void)modifiers; if (key < RETROK_LAST) callback_keys[key] = down; }
int lr_pad_action(int function)
{ return function >= 0 && function < NUMGAMEFUNCTIONS && pad_actions[function]; }
void lr_pad_clear(int function)
{
    if (function >= 0 && function < NUMGAMEFUNCTIONS && pad_actions[function]) {
        pad_consumed[function] = 1; pad_actions[function] = 0;
    }
}

void lr_input_poll(retro_input_state_t state, unsigned device)
{
    uint8_t keys[RETROK_LAST];
    unsigned i;
    lr_set_input_device(device);
    memset(keys, 0, sizeof(keys));
    memset(pad_actions, 0, sizeof(pad_actions));
    for (i = 0; i < RETROK_LAST; ++i)
        if ((device==RETRO_DEVICE_MOUSE || device==RETRO_DEVICE_KEYBOARD) && scancodes[i])
            keys[i] = callback_keys[i] || (state && state(0, RETRO_DEVICE_KEYBOARD, 0, i) != 0);
    if (!state) goto keyboard;
    if(device==RETRO_DEVICE_MOUSE || device==RETRO_DEVICE_KEYBOARD) {
        mouse_x += state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
        mouse_y += state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
        mouse_buttons = (state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT) ? 1 : 0)
          | (state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT) ? 2 : 0)
          | (state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE) ? 4 : 0)
          | (state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP) ? 8 : 0)
          | (state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN) ? 16 : 0);
    }
    if (device==RETRO_DEVICE_JOYPAD || device==RETRO_DEVICE_ANALOG) {
        int lx = state(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
        int ly = state(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
        int rx = state(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
        int ry = state(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
        int menu = !(ps[myconnectindex].gm & MODE_GAME) || (ps[myconnectindex].gm & MODE_MENU);
        pad_actions[gamefunc_Move_Forward] = pad(state, RETRO_DEVICE_ID_JOYPAD_UP) || ly < -8192;
        pad_actions[gamefunc_Move_Backward] = pad(state, RETRO_DEVICE_ID_JOYPAD_DOWN) || ly > 8192;
        pad_actions[gamefunc_Turn_Left] = pad(state, RETRO_DEVICE_ID_JOYPAD_LEFT);
        pad_actions[gamefunc_Turn_Right] = pad(state, RETRO_DEVICE_ID_JOYPAD_RIGHT);
        pad_actions[gamefunc_Strafe_Left] = lx < -8192;
        pad_actions[gamefunc_Strafe_Right] = lx > 8192;
        pad_actions[gamefunc_Jump] = pad(state, RETRO_DEVICE_ID_JOYPAD_A);
        pad_actions[gamefunc_Fire] = pad(state, RETRO_DEVICE_ID_JOYPAD_R2);
        pad_actions[gamefunc_Open] = pad(state, RETRO_DEVICE_ID_JOYPAD_X);
        pad_actions[gamefunc_Strafe] = pad(state, RETRO_DEVICE_ID_JOYPAD_L2);
        pad_actions[gamefunc_Crouch] = pad(state, RETRO_DEVICE_ID_JOYPAD_B);
        pad_actions[gamefunc_Inventory] = pad(state, RETRO_DEVICE_ID_JOYPAD_Y);
        pad_actions[gamefunc_Map] = pad(state, RETRO_DEVICE_ID_JOYPAD_SELECT);
        pad_actions[gamefunc_Previous_Weapon] = pad(state, RETRO_DEVICE_ID_JOYPAD_L);
        pad_actions[gamefunc_Next_Weapon] = pad(state, RETRO_DEVICE_ID_JOYPAD_R);
        pad_actions[gamefunc_Run] = pad(state, RETRO_DEVICE_ID_JOYPAD_L3);
        pad_actions[gamefunc_Mouse_Aiming] = pad(state, RETRO_DEVICE_ID_JOYPAD_R3);
        if (menu) {
            keys[RETROK_UP] |= pad_actions[gamefunc_Move_Forward];
            keys[RETROK_DOWN] |= pad_actions[gamefunc_Move_Backward];
            keys[RETROK_LEFT] |= pad_actions[gamefunc_Turn_Left] || lx < -8192;
            keys[RETROK_RIGHT] |= pad_actions[gamefunc_Turn_Right] || lx > 8192;
            keys[RETROK_RETURN] |= pad_actions[gamefunc_Jump] || pad_actions[gamefunc_Inventory];
            memset(pad_actions, 0, sizeof(pad_actions));
        }
        keys[RETROK_ESCAPE] |= pad(state, RETRO_DEVICE_ID_JOYPAD_START);
        if (abs(rx) > 8192) mouse_x += rx / 2048;
        if (abs(ry) > 8192) mouse_y += ry / 2048;
    }
keyboard:
    for (i = 0; i < NUMGAMEFUNCTIONS; ++i) {
        if (!pad_actions[i]) pad_consumed[i] = 0;
        if (pad_consumed[i]) pad_actions[i] = 0;
    }
    for (i = 0; i < RETROK_LAST; ++i) {
        if (keys[i] != previous[i] && scancodes[i]) sendkey(scancodes[i], keys[i]);
        previous[i] = keys[i];
    }
}

void lr_video_convert(uint32_t *out)
{
    unsigned i;
    for (i = 0; i < sizeof(pixels); ++i) out[i] = colors[pixels[i]];
}

void _platform_init(int argc, char **argv, const char *title, const char *icon)
{ (void)title; (void)icon; _argc = argc; _argv = argv; }
void _handle_events(void)
{
    if (lr_quit) { lr_quit = 0; ud.showcinematics = 0; gameexit(""); }
    /* Busy waits (intro, animation, keyboard prompts) must also return to the frontend. */
    if (++event_budget >= 256) { event_budget = 0; lr_yield(); }
}
void _idle(void) { _handle_events(); lr_yield(); }
void _nextpage(void) { total_rendered_frames++; event_budget = 0; lr_yield(); }
void _updateScreenRect(int32_t x, int32_t y, int32_t w, int32_t h)
{ (void)x; (void)y; (void)w; (void)h; }
void *_getVideoBase(void) { return pixels; }
void *get_framebuffer(void) { return pixels; }
void initkeys(void) { }
void uninitkeys(void) { }
uint8_t _readlastkeyhit(void) { return lastkey; }
void _joystick_init(void) { }
void _joystick_deinit(void) { }
int _joystick_update(void) { return 0; }
int _joystick_axis(int axis) { (void)axis; return 0; }
int _joystick_hat(int hat) { (void)hat; return 0; }
int _joystick_button(int button) { (void)button; return 0; }
int setupmouse(void) { moustat = 1; return 1; }
void readmousexy(short *x, short *y)
{
    *x = max(-32768, min(32767, mouse_x)); *y = max(-32768, min(32767, mouse_y));
    mouse_x = mouse_y = 0;
}
void readmousebstatus(short *buttons) { *buttons = mouse_buttons; mouse_buttons &= 7; }
void setvmode(int mode) { (void)mode; }
void getvalidvesamodes(void)
{ validmodecnt = 1; validmode[0] = 1; validmodexdim[0] = LR_WIDTH; validmodeydim[0] = LR_HEIGHT; vesachecked = 1; }
int32_t _setgamemode(uint8_t option, int32_t width, int32_t height)
{
    int i;
    (void)option; (void)width; (void)height;
    xdim = xres = LR_WIDTH; ydim = yres = LR_HEIGHT; bytesperline = LR_WIDTH;
    imageSize = sizeof(pixels); maxpages = 1; qsetmode = 200;
    frameplace = frameoffset = pixels;
    linearmode = vgacompatible = vesachecked = 1; activepage = visualpage = 0;
    free(horizlookup); free(horizlookup2);
    horizlookup = calloc(ydim * 4, sizeof(int32_t));
    horizlookup2 = calloc(ydim * 4, sizeof(int32_t));
    if (!horizlookup || !horizlookup2) lr_exit(1);
    for (i = 0; i <= ydim; ++i) ylookup[i] = i * bytesperline;
    horizycent = ydim * 2; oxyaspect = oxdimen = oviewingrange = -1;
    setBytesPerLine(bytesperline); setview(0, 0, xdim - 1, ydim - 1);
    setbrightness(curbrightness, palette);
    if (searchx < 0) { searchx = halfxdimen; searchy = ydimen >> 1; }
    return 0;
}
int VBE_setPalette(uint8_t *p)
{
    unsigned i;
    memcpy(dac, p, sizeof(dac));
    for (i = 0; i < 256; ++i)
        colors[i] = ((p[i*4+2] * 255 / 63) << 16) | ((p[i*4+1] * 255 / 63) << 8) | (p[i*4] * 255 / 63);
    return 1;
}
int VBE_getPalette(int32_t start, int32_t count, uint8_t *p)
{ if (start < 0 || count < 0 || start + count > 256) return 0; memcpy(p + start*4, dac + start*4, count*4); return 1; }
void _uninitengine(void)
{
    unsigned i;
    free(horizlookup); free(horizlookup2); horizlookup = horizlookup2 = NULL;
    for (i = 0; i < NUM_SOUNDS; ++i) { Sound[i].ptr = NULL; Sound[i].lock = Sound[i].num = 0; }
}
int screencapture(char *file, uint8_t inverse) { (void)file; (void)inverse; return -1; }
uint8_t readpixel(uint8_t *p) { return *p; }
void drawpixel(uint8_t *p, uint8_t color) { *p = color; }
void setcolor16(uint8_t color) { drawcolor = color; }
void drawpixel16(int32_t offset) { if (offset >= 0 && offset < sizeof(pixels)) pixels[offset] = drawcolor; }
void fillscreen16(int32_t offset, int32_t color, int32_t count)
{ if (offset >= 0 && count > 0 && offset < sizeof(pixels)) memset(pixels + offset, color, min(count, sizeof(pixels)-offset)); }
void clear2dscreen(void) { memset(pixels, 0, sizeof(pixels)); }
void drawline16(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t color)
{
    int dx = abs(x1-x0), sx = x0<x1 ? 1 : -1, dy = -abs(y1-y0), sy = y0<y1 ? 1 : -1, err = dx+dy;
    for (;;) {
        int e2;
        if (x0 >= 0 && x0 < LR_WIDTH && y0 >= 0 && y0 < LR_HEIGHT) pixels[y0*LR_WIDTH+x0] = color;
        if (x0 == x1 && y0 == y1) break;
        e2 = 2*err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
int inittimer(int rate) { timer_rate = rate; timer_sample = lr_frame * rate / 60; return 0; }
void uninittimer(void) { timer_rate = 0; timer_callback = NULL; }
void (*installusertimercallback(void (*callback)(void)))(void)
{ void (*old)(void) = timer_callback; timer_callback = callback; return old; }
void sampletimer(void)
{
    uint64_t now;
    _handle_events();
    now = lr_frame * timer_rate / 60;
    while (timer_sample < now) { ++timer_sample; ++totalclock; if (timer_callback) timer_callback(); }
}
uint32_t getticks(void) { return (uint32_t)(lr_frame * 1000 / 60); }
int gettimerfreq(void) { return timer_rate; }

void lr_apply_options(void)
{
    struct retro_variable aim = {"chocolate_duke3d_autoaim", NULL};
    struct retro_variable crosshair = {"chocolate_duke3d_crosshair", NULL};
    if (!lr_environment) return;
    if (lr_environment(RETRO_ENVIRONMENT_GET_VARIABLE, &aim) && aim.value)
        ud.auto_aim = !strcmp(aim.value, "Off") ? 0 :
            !strcmp(aim.value, "Bullet only") ? 1 : 2;
    if (lr_environment(RETRO_ENVIRONMENT_GET_VARIABLE, &crosshair) && crosshair.value)
        ud.crosshair = strcmp(crosshair.value, "Off") != 0;
}

void lr_game_config(void)
{
    ScreenWidth = LR_WIDTH; ScreenHeight = LR_HEIGHT; ScreenMode = 1;
    MixRate = LR_RATE; NumBits = 16; NumChannels = 2; NumVoices = 32;
    enable_mouselook();
    ControllerType = controltype_keyboardandmouse;
    // Build's getmousevalues() discards all motion while moustat is zero.
    // The SDL-free backend must enable this reader for mice and right sticks.
    setupmouse();
    lr_apply_options();
}
