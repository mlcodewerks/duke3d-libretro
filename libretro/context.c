/* Cooperative game execution. All engine and frontend work stays on one thread. */
#include <stdlib.h>
#include "core_internal.h"
#ifdef _WIN32
#include <windows.h>
static void *frontend, *game;
static int converted;
static void (*game_entry)(void);
static VOID WINAPI enter_game(void *unused) { (void)unused; game_entry(); lr_exit(0); }
int lr_context_create(void (*entry)(void))
{
    game_entry = entry;
    if (IsThreadAFiber()) frontend = GetCurrentFiber();
    else { frontend = ConvertThreadToFiber(NULL); converted = frontend != NULL; }
    if (!frontend) return 0;
    game = CreateFiber(8 * 1024 * 1024, enter_game, NULL);
    if (!game) lr_context_destroy();
    return game != NULL;
}
void lr_context_resume(void) { SwitchToFiber(game); }
void lr_yield(void) { SwitchToFiber(frontend); }
void lr_context_destroy(void)
{
    if (game) DeleteFiber(game);
    game = NULL;
    if (converted) ConvertFiberToThread();
    converted = 0; frontend = NULL;
}
#else
#include <ucontext.h>
static ucontext_t frontend, game;
static void *stack;
int lr_context_create(void (*entry)(void))
{
    stack = malloc(8 * 1024 * 1024);
    if (!stack) return 0;
    if (getcontext(&game) != 0) { free(stack); stack = NULL; return 0; }
    game.uc_stack.ss_sp = stack;
    game.uc_stack.ss_size = 8 * 1024 * 1024;
    game.uc_link = &frontend;
    makecontext(&game, entry, 0);
    return 1;
}
void lr_context_resume(void) { swapcontext(&frontend, &game); }
void lr_yield(void) { swapcontext(&game, &frontend); }
void lr_context_destroy(void) { free(stack); stack = NULL; }
#endif
