#pragma once

#include <SDL3/SDL.h>

namespace app {

// SDL3 callback entry points, forwarded from main.cpp.
SDL_AppResult init();
SDL_AppResult iterate();
SDL_AppResult event(const SDL_Event* event);
void quit();

} // namespace app
