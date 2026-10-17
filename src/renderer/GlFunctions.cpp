#include "renderer/GlFunctions.h"

#if !defined(CHI_GLES)

#include <SDL3/SDL.h>

#define CHI_GL_DEFINE(ret, name, args) ret(APIENTRY* chi_##name) args = nullptr;
CHI_GL_FUNCTIONS(CHI_GL_DEFINE)
#undef CHI_GL_DEFINE

bool loadGlFunctions() {
    bool ok = true;
#define CHI_GL_LOAD(ret, name, args) \
    chi_##name = reinterpret_cast<decltype(chi_##name)>(SDL_GL_GetProcAddress(#name)); \
    ok = ok && chi_##name != nullptr;
    CHI_GL_FUNCTIONS(CHI_GL_LOAD)
#undef CHI_GL_LOAD
    return ok;
}

#else

bool loadGlFunctions() {
    return true;
}

#endif
