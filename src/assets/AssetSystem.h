#pragma once

#include "assets/CharacterModel.h"
#include "assets/StaticModel.h"

namespace assets {

class AssetSystem {
public:
    // Loads every asset the game needs. Paths are relative to the working
    // directory on desktop and the preloaded virtual filesystem on web.
    bool init();
    void cleanup();

    const CharacterModel& character() const;

    // Weapon props. A missing file leaves the prop empty and the game runs without it.
    const StaticMesh& rifle() const;
    const StaticMesh& pistol() const;

private:
    CharacterModel character_;
    StaticModel rifle_;
    StaticModel pistol_;
};

} // namespace assets
