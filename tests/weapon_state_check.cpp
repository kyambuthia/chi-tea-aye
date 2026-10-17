// Checks the weapon state machine: switching between rifle, pistol, and fists,
// rapid requests, the fist attack, and rifle-only aiming.
// Build and run from the repository root:
//   cmake --build build --target weapon_state_check && ./build/weapon_state_check

#include "gameplay/WeaponState.h"

#include <cstdio>

using gameplay::WeaponSlot;
using gameplay::WeaponState;

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("  FAIL: %s\n", what);
    }
}

void run(WeaponState& w, float seconds) {
    const int steps = static_cast<int>(seconds * 60.0f + 0.5f);
    for (int i = 0; i < steps; ++i) {
        w.update(1.0f / 60.0f);
    }
}

} // namespace

int main() {
    {
        WeaponState w;
        expect(w.active() == WeaponSlot::Rifle, "starts with the rifle");
        expect(w.phase() == WeaponState::Phase::Ready, "starts ready");
        expect(w.raise() == 1.0f, "starts fully raised");
    }
    {
        WeaponState w;
        w.request(WeaponSlot::Rifle);
        expect(w.phase() == WeaponState::Phase::Ready, "requesting the equipped weapon does nothing");
    }
    {
        WeaponState w;
        w.request(WeaponSlot::Pistol);
        expect(w.phase() == WeaponState::Phase::Lowering, "switching starts by lowering");
        expect(w.active() == WeaponSlot::Rifle, "the rifle stays active while lowering");
        run(w, WeaponState::kSwitchTime + 0.01f);
        expect(w.active() == WeaponSlot::Pistol, "the pistol is active once the rifle is stowed");
        expect(w.phase() == WeaponState::Phase::Raising, "then the pistol is raised");
        run(w, WeaponState::kSwitchTime + 0.01f);
        expect(w.phase() == WeaponState::Phase::Ready, "the switch completes");
        expect(w.raise() == 1.0f, "fully raised after the switch");
    }
    {
        // Requests during the lowering half retarget it: the last one wins.
        WeaponState w;
        w.request(WeaponSlot::Pistol);
        run(w, 0.1f);
        w.request(WeaponSlot::Fists);
        run(w, WeaponState::kSwitchTime * 2.0f);
        expect(w.active() == WeaponSlot::Fists, "a rapid second request wins");
        expect(w.phase() == WeaponState::Phase::Ready, "rapid switching settles to ready");
    }
    {
        // Asking for the weapon being stowed cancels the switch and does not get stuck.
        WeaponState w;
        w.request(WeaponSlot::Pistol);
        run(w, 0.1f);
        w.request(WeaponSlot::Rifle);
        run(w, WeaponState::kSwitchTime * 2.0f);
        expect(w.active() == WeaponSlot::Rifle, "cancelling back to the rifle leaves the rifle active");
        expect(w.phase() == WeaponState::Phase::Ready, "cancelled switch settles to ready");
    }
    {
        // A request during the raising half restarts the switch toward it.
        WeaponState w;
        w.request(WeaponSlot::Pistol);
        run(w, WeaponState::kSwitchTime + 0.1f);
        w.request(WeaponSlot::Fists);
        expect(w.phase() == WeaponState::Phase::Lowering, "a request while raising lowers again");
        run(w, WeaponState::kSwitchTime * 3.0f);
        expect(w.active() == WeaponSlot::Fists && w.phase() == WeaponState::Phase::Ready,
            "a request while raising settles on the newest weapon");
    }
    {
        // Fists attack only when the fists are ready in hand; a punch lasts its length.
        WeaponState w;
        w.attack();
        expect(!w.attacking(), "no attack with the rifle in hand");
        w.request(WeaponSlot::Fists);
        run(w, WeaponState::kSwitchTime * 2.0f + 0.01f);
        w.attack();
        expect(w.attacking(), "an attack starts with fists ready");
        run(w, WeaponState::kAttackTime * 0.5f);
        expect(w.attacking(), "the punch is still playing halfway through");
        w.attack();
        run(w, WeaponState::kAttackTime);
        expect(!w.attacking(), "the punch ends after its length");
    }
    {
        // Aiming: rifle only, and it eases in and out.
        WeaponState w;
        w.setAiming(true);
        run(w, WeaponState::kAimTime * 0.5f);
        expect(w.aim() > 0.0f && w.aim() < 1.0f, "aim eases in");
        run(w, WeaponState::kAimTime);
        expect(w.aim() == 1.0f, "fully aimed with the rifle");
        w.setAiming(false);
        run(w, WeaponState::kAimTime * 2.0f);
        expect(w.aim() == 0.0f, "aim releases back to hip");
    }
    {
        WeaponState w;
        w.request(WeaponSlot::Pistol);
        run(w, WeaponState::kSwitchTime * 2.0f + 0.01f);
        w.setAiming(true);
        run(w, WeaponState::kAimTime * 2.0f);
        expect(w.aim() == 0.0f, "no aiming with the pistol");
    }

    std::printf(failures == 0 ? "WEAPON STATE OK\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
