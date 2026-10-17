#include "gameplay/WeaponState.h"

#include <algorithm>

namespace gameplay {

void WeaponState::request(WeaponSlot slot) {
    if (phase_ == Phase::Ready) {
        if (slot == active_) {
            return;
        }
        target_ = slot;
        phase_ = Phase::Lowering;
        phase_time_ = 0.0f;
        return;
    }
    // Mid-switch: the latest request wins. Asking for the weapon being stowed
    // resumes it at once, so the switch is cancelled rather than left stuck.
    if (phase_ == Phase::Lowering) {
        target_ = slot;
        return;
    }
    // Raising: finish drawing the current target, then switch again if needed.
    if (slot != target_) {
        target_ = slot;
        phase_ = Phase::Lowering;
        phase_time_ = 0.0f;
    }
}

void WeaponState::attack() {
    if (phase_ == Phase::Ready && active_ == WeaponSlot::Fists && attack_time_ <= 0.0f) {
        attack_time_ = kAttackTime;
    }
}

void WeaponState::setAiming(bool aiming) {
    aiming_ = aiming;
}

void WeaponState::update(float delta_time) {
    if (attack_time_ > 0.0f) {
        attack_time_ = std::max(0.0f, attack_time_ - delta_time);
    }

    if (phase_ == Phase::Lowering) {
        phase_time_ += delta_time;
        if (phase_time_ >= kSwitchTime) {
            active_ = target_;
            phase_ = Phase::Raising;
            phase_time_ = 0.0f;
        }
    } else if (phase_ == Phase::Raising) {
        phase_time_ += delta_time;
        if (phase_time_ >= kSwitchTime) {
            phase_ = Phase::Ready;
            phase_time_ = 0.0f;
        }
    }

    // Aiming is only possible with the rifle in hand and ready.
    const bool can_aim = aiming_ && phase_ == Phase::Ready && active_ == WeaponSlot::Rifle;
    const float goal = can_aim ? 1.0f : 0.0f;
    const float step = delta_time / kAimTime;
    if (aim_ < goal) {
        aim_ = std::min(goal, aim_ + step);
    } else if (aim_ > goal) {
        aim_ = std::max(goal, aim_ - step);
    }
}

float WeaponState::raise() const {
    if (phase_ == Phase::Lowering) {
        return 1.0f - phase_time_ / kSwitchTime;
    }
    if (phase_ == Phase::Raising) {
        return phase_time_ / kSwitchTime;
    }
    return 1.0f;
}

float WeaponState::attackProgress() const {
    return attack_time_ > 0.0f ? 1.0f - attack_time_ / kAttackTime : 0.0f;
}

} // namespace gameplay
