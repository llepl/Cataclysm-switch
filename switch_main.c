/* Starts the game on a thread with a big stack.
 *
 * The Switch gives the main thread only about 1 MB of stack. Some functions of the
 * 2010 game (e.g. overmap::draw, which has a ~1.8 MB stack frame) need much more, so
 * the game would crash with an invalid memory access. The game's own main() is renamed
 * to game_main() (see Makefile.switch: -Dmain=game_main) and called from here.
 *
 * Quitting: a process must be ended from the main thread. If the game calls exit() on
 * its own thread, exit() is redirected here (-Wl,--wrap=exit): the game thread just
 * ends and the main thread leaves normally.
 */
#include <switch.h>
#include <stdlib.h>
#include <malloc.h>

/* main() of the game, compiled as C++: its mangled name is _Z9game_mainiPPc */
extern int game_main(int argc, char **argv) __asm__("_Z9game_mainiPPc");
extern void __real_exit(int code) __attribute__((noreturn));

#define GAME_STACK_SIZE (32u * 1024u * 1024u)

static int    g_argc;
static char **g_argv;
static int    g_result;
static void  *g_game_tls;          /* identifies the game thread */

static void game_thread(void *arg)
{
    (void)arg;
    g_game_tls = armGetTls();
    g_result = game_main(g_argc, g_argv);
}

void __wrap_exit(int code)
{
    if (g_game_tls != NULL && armGetTls() == g_game_tls) {
        g_result = code;
        svcExitThread();
        for (;;) { }
    }
    __real_exit(code);
}

int main(int argc, char **argv)
{
    Thread thread;
    void  *stack;
    Result rc;

    g_argc = argc;
    g_argv = argv;

    stack = memalign(0x1000, GAME_STACK_SIZE);
    if (stack == NULL)
        return game_main(argc, argv);          /* no memory: run on the small stack */

    rc = threadCreate(&thread, game_thread, NULL, stack, GAME_STACK_SIZE, 0x2C, -2);
    if (R_FAILED(rc)) {
        free(stack);
        return game_main(argc, argv);
    }

    rc = threadStart(&thread);
    if (R_FAILED(rc)) {
        threadClose(&thread);
        free(stack);
        return game_main(argc, argv);
    }

    threadWaitForExit(&thread);
    threadClose(&thread);
    free(stack);
    return g_result;
}
