#include "assets/AssetSystem.h"

#include <SDL3/SDL.h>

namespace assets {
namespace {

constexpr const char* kWarriorPath = "assets/characters/african_warrior.glb";

} // namespace

bool AssetSystem::init() {
    size_t size = 0;
    void* bytes = SDL_LoadFile(kWarriorPath, &size);
    if (bytes == nullptr) {
        SDL_Log("failed to read %s: %s", kWarriorPath, SDL_GetError());
        return false;
    }

    const bool ok = character_.load(bytes, size);
    SDL_free(bytes);
    if (!ok) {
        SDL_Log("failed to parse %s", kWarriorPath);
    }
    return ok;
}

void AssetSystem::cleanup() {
    character_.cleanup();
}

const CharacterModel& AssetSystem::character() const {
    return character_;
}

} // namespace assets
