#pragma once

#include <cmath>

namespace renderer {

// Sun, sky, ambient and fog for a time of day. Pure data, no GL, so it can be checked
// headless and shared by every pass that lights or fogs something.
//
// The look aims at a hazy, warm city: dense distance fog that hides the far plane,
// a low golden sun in the afternoon, and sodium-orange haze at night.
struct Atmosphere {
    float sun_dir[3];      // unit, toward the sun (or the moon at night)
    float sun_color[3];
    float sky_zenith[3];
    float sky_horizon[3];  // also the fog colour, so distant buildings melt into the sky
    float ambient_sky[3];
    float ambient_ground[3];
    float fog_density;     // per metre, for exp(-density * distance)
    float night;           // 0 by day, 1 at night: lit windows and lamps
};

namespace atmosphere_detail {
inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline void mix3(const float a[3], const float b[3], float t, float out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = a[i] + (b[i] - a[i]) * t;
}
} // namespace atmosphere_detail

// hour: 0..24. density_scale multiplies the fog (1 is the default haze).
inline Atmosphere atmosphereAt(float hour, float density_scale = 1.0f) {
    using namespace atmosphere_detail;
    constexpr float kPi = 3.14159265f;
    hour = std::fmod(std::fmod(hour, 24.0f) + 24.0f, 24.0f);
    // The sun rises at 6, peaks at 13 slightly to the south, and sets at 20.
    const float day_phase = (hour - 6.0f) / 14.0f;  // 0 at sunrise, 1 at sunset
    const float elevation = std::sin(day_phase * kPi) * 1.05f;  // radians; negative at night
    const float azimuth = (day_phase - 0.5f) * kPi * 1.1f + 0.6f;

    Atmosphere a{};
    // At night the light comes from a high moon instead.
    const bool up = elevation > 0.0f;
    const float light_elevation = up ? elevation : 0.9f;
    a.sun_dir[0] = std::cos(light_elevation) * std::sin(azimuth);
    a.sun_dir[1] = std::sin(light_elevation);
    a.sun_dir[2] = std::cos(light_elevation) * std::cos(azimuth);

    // Day: 0 at night, 1 with the sun well up. Dusk: peaks with the sun on the horizon.
    const float day = clamp01(elevation / 0.35f + 0.25f);
    const float dusk = clamp01(1.0f - std::fabs(elevation) / 0.45f);
    a.night = clamp01(1.0f - day * 1.6f);

    const float noon_sun[3] = {1.0f, 0.95f, 0.86f};
    const float dusk_sun[3] = {1.0f, 0.62f, 0.34f};
    const float moon[3] = {0.20f, 0.24f, 0.34f};
    float sun[3];
    mix3(noon_sun, dusk_sun, dusk, sun);
    mix3(moon, sun, day, a.sun_color);

    const float day_zenith[3] = {0.36f, 0.50f, 0.68f};
    const float night_zenith[3] = {0.02f, 0.03f, 0.06f};
    const float day_horizon[3] = {0.70f, 0.70f, 0.66f};    // pale, hazy
    const float dusk_horizon[3] = {0.86f, 0.58f, 0.38f};   // golden
    const float night_horizon[3] = {0.20f, 0.13f, 0.08f};  // sodium-lit haze
    float horizon[3];
    mix3(day_horizon, dusk_horizon, dusk, horizon);
    mix3(night_horizon, horizon, day, a.sky_horizon);
    mix3(night_zenith, day_zenith, day, a.sky_zenith);

    const float day_amb_sky[3] = {0.50f, 0.54f, 0.60f};
    const float night_amb_sky[3] = {0.07f, 0.07f, 0.10f};
    const float day_amb_ground[3] = {0.32f, 0.29f, 0.25f};
    const float night_amb_ground[3] = {0.10f, 0.06f, 0.03f};  // street light bounce
    mix3(night_amb_sky, day_amb_sky, day, a.ambient_sky);
    mix3(night_amb_ground, day_amb_ground, day, a.ambient_ground);

    a.fog_density = (0.0055f + 0.003f * dusk + 0.002f * a.night) * density_scale;
    return a;
}

} // namespace renderer
