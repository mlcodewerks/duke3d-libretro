/* Only the legacy menu grab flag and mixer locks remain. No SDL runtime.
 * Cooperative execution makes the mixer locks unnecessary. */
#ifndef DUKE_RETRO_SDL_COMPAT_H
#define DUKE_RETRO_SDL_COMPAT_H
#include <stdlib.h>
#include <stdint.h>
#ifndef cdecl
#define cdecl
#endif
typedef struct { int unused; } SDL_mutex;
typedef struct { void *pixels; int w, h; } SDL_Surface;
#define SDL_GRAB_QUERY -1
#define SDL_GRAB_OFF 0
#define SDL_GRAB_ON 1
#define SDL_INIT_VIDEO 0
static inline SDL_mutex *SDL_CreateMutex(void) { return (SDL_mutex *)malloc(sizeof(SDL_mutex)); }
static inline void SDL_DestroyMutex(SDL_mutex *m) { free(m); }
static inline int SDL_mutexP(SDL_mutex *m) { (void)m; return 0; }
static inline int SDL_mutexV(SDL_mutex *m) { (void)m; return 0; }
static inline int SDL_WM_GrabInput(int mode) { (void)mode; return SDL_GRAB_ON; }
static inline void SDL_ShowCursor(int mode) { (void)mode; }
static inline void SDL_Quit(void) { }
static inline void SDL_QuitSubSystem(int flags) { (void)flags; }
#endif
