#define SDL_MAIN_USE_CALLBACKS 1

#include "app/App.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    (void)appstate;
    (void)argc;
    (void)argv;
    return app::init();
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    (void)appstate;
    return app::iterate();
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    (void)appstate;
    return app::event(event);
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    (void)appstate;
    (void)result;
    app::quit();
}
