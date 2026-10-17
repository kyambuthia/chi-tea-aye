#pragma once

#include "assets/CharacterModel.h"

namespace assets {

class AssetSystem {
public:
    // Loads every asset the game needs. Paths are relative to the working
    // directory on desktop, the preloaded virtual filesystem on web, and the
    // APK assets folder on Android.
    bool init();
    void cleanup();

    const CharacterModel& character() const;

private:
    CharacterModel character_;
};

} // namespace assets
