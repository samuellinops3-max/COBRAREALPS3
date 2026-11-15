/* COBRA ARENA - main.c (junta as 4 partes). Deixe parte1.h..parte4.h na mesma pasta do main.c */
#include "parte1.h"
#include "parte2.h"
#include "parte3.h"
#include "parte4.h"

/* main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    Uint32 last, now;
    int i;
    (void)argc;
    (void)argv;

#ifdef PS3_BUILD
    sysUtilRegisterCallback(0, sysutil_cb, NULL);
#endif

    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
    SDL_InitSubSystem(SDL_INIT_JOYSTICK);

    screen = SDL_SetVideoMode(SW, SH, 32, SDL_SWSURFACE);
    if (!screen) { SDL_Quit(); return 1; }
    SDL_ShowCursor(SDL_DISABLE);

    rsh = screen->format->Rshift;
    gsh = screen->format->Gshift;
    bsh = screen->format->Bshift;

    rs ^= (unsigned int)SDL_GetTicks() * 2654435761u;
    if (rs == 0) rs = 2463534242u;

    memset(sn, 0, sizeof(sn));
    memset(food, 0, sizeof(food));

    build_bg();
    open_joys();

    for (i = 0; i < NFOOD_TARGET; i++) spawn_ambient_food();
    for (i = 0; i < NSNAKE; i++) spawn_snake(i);

    last = SDL_GetTicks();
    while (g_running) {
        SDL_Event e;
        float dt;

#ifdef PS3_BUILD
        sysUtilCheckCallback();
#endif
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) g_running = 0;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) g_running = 0;
        }

        now = SDL_GetTicks();
        dt = (float)(now - last) / 1000.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;
        if (dt < 0.001f) dt = 0.001f;

        update(dt);
        render();
        SDL_Flip(screen);
    }

    for (i = 0; i < 4; i++) if (joys[i]) SDL_JoystickClose(joys[i]);
    SDL_Quit();
    return 0;
}
