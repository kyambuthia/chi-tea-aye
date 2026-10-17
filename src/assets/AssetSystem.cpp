#include "assets/AssetSystem.h"

#include <SDL3/SDL.h>

namespace assets {
namespace {

constexpr const char* kWarriorPath = "assets/characters/african_warrior.glb";
constexpr const char* kRiflePath = "assets/models/rifle.glb";
constexpr const char* kPistolPath = "assets/models/pistol.glb";

// Real-world lengths of the props, in metres.
constexpr float kRifleLength = 0.95f;
constexpr float kPistolLength = 0.22f;

// Loads a prop GLB if it is present. Returns false and logs otherwise.
bool loadProp(StaticModel& prop, const char* path, float length) {
    size_t size = 0;
    void* bytes = SDL_LoadFile(path, &size);
    if (bytes == nullptr) {
        SDL_Log("weapon prop %s not loaded: %s", path, SDL_GetError());
        return false;
    }
    const bool ok = prop.load(bytes, size, length);
    SDL_free(bytes);
    if (!ok) {
        SDL_Log("weapon prop %s has no usable geometry", path);
    }
    return ok;
}

} // namespace

bool AssetSystem::init() {
    loadProp(rifle_, kRiflePath, kRifleLength);
    loadProp(pistol_, kPistolPath, kPistolLength);

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

const StaticMesh& AssetSystem::rifle() const {
    return rifle_.mesh();
}

const StaticMesh& AssetSystem::pistol() const {
    return pistol_.mesh();
}

} // namespace assets
