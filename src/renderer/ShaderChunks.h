#pragma once

#include "renderer/Atmosphere.h"
#include "renderer/GlApi.h"

namespace renderer {

// GLSL shared by every lit pass: hemisphere ambient, distance fog that fades into the
// horizon colour, a cheap hash, and the street-lamp light pools at night.
// The lamp layout mirrors world::City (lamps 0.8 m in from each curb, every 15 m).
inline const char* kAtmosphereGlsl = R"(
uniform vec3 eye_pos;
uniform vec3 sun_dir;
uniform vec3 sun_color;
uniform vec3 amb_sky;
uniform vec3 amb_ground;
uniform vec3 fog_color;
uniform vec2 fog_params;  // x: density per metre, y: night (0..1)

vec3 hemiAmbient(vec3 n) {
    return mix(amb_ground, amb_sky, n.y * 0.5 + 0.5);
}

vec3 applyFog(vec3 c, vec3 wp) {
    float d = length(wp - eye_pos);
    float f = 1.0 - exp(-d * fog_params.x);
    // Haze hugs the ground: tall rooftops stand out of it a little.
    f *= 1.0 - 0.4 * clamp((wp.y - 15.0) / 150.0, 0.0, 1.0);
    return mix(c, fog_color, f);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Smooth value noise in 0..1: bilinear between hashed lattice points, so ground
// grain reads as texture instead of a grid of blocks.
float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Ground grain: fine speckle plus broad patches, centred on zero.
float groundGrain(vec2 p) {
    return (valueNoise(p * 2.5) - 0.5) * 0.035 + (valueNoise(p * 0.3) - 0.5) * 0.06;
}

// Sodium light pools on the ground around each street lamp at night.
float lampPools(vec2 p) {
    vec2 cell = p - 60.0 * floor(p / 60.0 + 0.5);  // block-local
    float pool = 0.0;
    for (int axis = 0; axis < 2; ++axis) {
        float across = axis == 0 ? cell.x : cell.y;
        float along = axis == 0 ? cell.y : cell.x;
        float line = sign(across) * 21.2;
        float slot = clamp(floor(along / 15.0 + 0.5), -1.0, 1.0) * 15.0;
        vec2 d = vec2(across - line, along - slot);
        pool += exp(-dot(d, d) / 22.0);
    }
    return pool;
}
)";

// Locations of the kAtmosphereGlsl uniforms in one program.
struct AtmosphereUniforms {
    GLint eye = -1, sun_dir = -1, sun_color = -1, amb_sky = -1, amb_ground = -1, fog_color = -1, fog_params = -1;

    void locate(GLuint program) {
        eye = glGetUniformLocation(program, "eye_pos");
        sun_dir = glGetUniformLocation(program, "sun_dir");
        sun_color = glGetUniformLocation(program, "sun_color");
        amb_sky = glGetUniformLocation(program, "amb_sky");
        amb_ground = glGetUniformLocation(program, "amb_ground");
        fog_color = glGetUniformLocation(program, "fog_color");
        fog_params = glGetUniformLocation(program, "fog_params");
    }

    // The program must be in use.
    void set(const Atmosphere& a, const float eye_pos[3]) const {
        glUniform3fv(eye, 1, eye_pos);
        glUniform3fv(sun_dir, 1, a.sun_dir);
        glUniform3fv(sun_color, 1, a.sun_color);
        glUniform3fv(amb_sky, 1, a.ambient_sky);
        glUniform3fv(amb_ground, 1, a.ambient_ground);
        glUniform3fv(fog_color, 1, a.sky_horizon);
        glUniform2f(fog_params, a.fog_density, a.night);
    }
};

} // namespace renderer
