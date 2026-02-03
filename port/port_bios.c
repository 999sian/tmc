#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdlib.h>

static bool gQuitRequested = false;

static void Port_PumpEvents(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) {
            gQuitRequested = true;
        }
    }
}

void VBlankIntrWait(void) {
    // 1) SDL events + input mapping ici
    Port_PumpEvents();

    // 2) Présentation vidéo: idéalement tu appelles ici ton "PresentFrame()"
    // PresentFrame();

    // 3) Limite à ~60 FPS (à ajuster)
    SDL_Delay(16);

    if (gQuitRequested) {
        exit(0);
    }
}
