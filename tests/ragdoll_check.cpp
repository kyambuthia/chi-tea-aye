// The warrior NPC falls, stays finite, and can be reset without affecting the player.
#include "app/InputState.h"
#include "assets/CharacterModel.h"
#include "gameplay/GameState.h"
#include "physics/Physics.h"
#include "world/World.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) {
            std::printf("FAIL: %s\n", message);
            ++failures;
        }
    };
    std::ifstream file("assets/characters/african_warrior.glb", std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    assets::CharacterModel warrior;
    if (!warrior.load(bytes.data(), bytes.size())) {
        std::puts("FAIL: could not load warrior");
        return 1;
    }
    world::World world;
    world.init();
    physics::Physics physics;
    physics.init(world);
    gameplay::GameState game;
    game.init();
    if (!game.spawnWarrior(physics, world, warrior)) {
        std::puts("FAIL: could not spawn warrior ragdoll");
        return 1;
    }
    constexpr float step = 1.0f / 60.0f;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    check(physics.ragdollHipsPosition(x, y, z), "initial hips position");
    const float initial_height = y;
    app::InputState input;
    bool finite = true;
    for (int frame = 0; frame < 600; ++frame) {
        game.update(input, physics, world, step);
        const math::Mat4* pose = game.warriorSkinPose();
        if (pose == nullptr) {
            finite = false;
            break;
        }
        for (int joint = 0; joint < warrior.jointCount(); ++joint) {
            for (float value : pose[joint].m) finite &= std::isfinite(value);
        }
    }
    check(finite, "NPC pose remains finite for 10 seconds");
    check(physics.ragdollHipsPosition(x, y, z), "settled hips position");
    std::printf("warrior hips: %.3f -> %.3f m, ground %.3f m\n", initial_height, y, world.groundHeight(x, z));
    check(y < initial_height - 0.3f, "warrior collapses under gravity");
    check(y > world.groundHeight(x, z) - 0.1f, "warrior stays above terrain");
    check(std::hypot(game.playerX(), game.playerZ()) < 0.001f, "NPC physics does not move the player");

    input.keys.w = true;
    for (int frame = 0; frame < 120; ++frame) game.update(input, physics, world, step);
    check(std::hypot(game.playerX(), game.playerZ()) > 2.0f, "player can move while NPC is a ragdoll");
    for (int reset = 0; reset < 12; ++reset) {
        const float player_x = game.playerX(), player_z = game.playerZ();
        input.ragdoll_pressed = true;
        game.update(input, physics, world, step);
        input.ragdoll_pressed = false;
        check(physics.ragdollHipsPosition(x, y, z) && std::fabs(x - 2.0f) < 0.3f && std::fabs(z - 3.5f) < 0.3f,
            "R respawns the NPC at its own spawn point");
        check(y > initial_height - 0.1f, "R restarts the fall");
        check(std::hypot(game.playerX() - player_x, game.playerZ() - player_z) < 0.2f,
            "R does not teleport the player");
        for (int frame = 0; frame < 60; ++frame) game.update(input, physics, world, step);
        check(game.warriorRagdollActive(), "ragdoll survives repeated resets");
    }
    game.cleanup();
    physics.cleanup();
    check(!physics.ragdollActive(), "cleanup removes the ragdoll");
    std::printf("ragdoll_check: %s (%d failures)\n", failures == 0 ? "passed" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
