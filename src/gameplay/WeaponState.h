#pragma once

namespace gameplay {

enum class WeaponSlot { Rifle, Pistol, Fists };

// Equipped weapon, the timed switch between weapons, the fist attack, and aiming.
//
// A switch stows the active weapon (Lowering) and then draws the requested one
// (Raising). Requests made while lowering retarget the switch, so the last
// request wins and the state never gets stuck.
class WeaponState {
public:
    enum class Phase { Ready, Lowering, Raising };

    static constexpr float kSwitchTime = 0.35f;  // seconds for each half of a switch
    static constexpr float kAttackTime = 0.70f;  // seconds for one punch
    static constexpr float kAimTime = 0.20f;     // seconds for the aim transition
    static constexpr float kFireTime = 1.17f;    // seconds of the RifleFire clip; one shot plays it once
    static constexpr float kFireInterval = 0.12f; // seconds between shots: a rifle fires about 8 rounds a second

    WeaponState() = default;

    // Requests a weapon. The same weapon already in hand is ignored.
    void request(WeaponSlot slot);

    // Fist attack; only acts while fists are ready in hand.
    void attack();

    // Rifle shot; only acts while the rifle is ready in hand and not already firing.
    // Returns true when a shot starts: the rifle is ready in hand and the fire interval has passed.
    bool fire();

    // Right mouse: aim only while the rifle is ready in hand.
    void setAiming(bool aiming);

    void update(float delta_time);

    WeaponSlot active() const { return active_; }
    WeaponSlot target() const { return target_; }
    Phase phase() const { return phase_; }

    // 0 while the weapon is fully stowed, 1 when it is fully raised.
    float raise() const;

    bool attacking() const { return attack_time_ > 0.0f; }
    float attackProgress() const;  // 0 at the start of a punch, 1 when it ends

    // The shot animation plays from the last shot. A new shot restarts it.
    bool firing() const { return shot_time_ < kFireTime; }
    float fireProgress() const;    // 0 at the start of the latest shot, 1 when its animation ends

    // Aim amount, 0 hip-fire to 1 fully aimed down sights.
    float aim() const { return aim_; }

private:
    WeaponSlot active_ = WeaponSlot::Rifle;
    WeaponSlot target_ = WeaponSlot::Rifle;
    Phase phase_ = Phase::Ready;
    float phase_time_ = 0.0f;
    float attack_time_ = 0.0f;
    float fire_cooldown_ = 0.0f;  // seconds until the next shot is allowed
    float shot_time_ = kFireTime; // seconds since the latest shot
    bool aiming_ = false;
    float aim_ = 0.0f;
};

} // namespace gameplay
