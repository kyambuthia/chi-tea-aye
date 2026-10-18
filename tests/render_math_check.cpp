// Checks the renderer's CPU-side maths: frustum culling keeps what is in view and drops
// what is not, the time of day gives a sensible sun, sky and night, and dynamic
// resolution drops under load, recovers when frames are on time, and ignores stalls.
// Build and run from the repository root:
//   cmake --build build --target render_math_check && ./build/render_math_check

#include "math/Frustum.h"
#include "math/Mat4.h"
#include "renderer/Atmosphere.h"
#include "renderer/ResolutionScale.h"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("  FAIL: %s\n", what);
    }
}

bool boxVisible(const math::Frustum& f, float x, float y, float z, float half) {
    const float lo[3] = {x - half, y - half, z - half};
    const float hi[3] = {x + half, y + half, z + half};
    return f.intersectsBox(lo, hi);
}

} // namespace

int main() {
    // 1. Frustum: a camera at the origin looking down -Z, 60 degrees, 16:9.
    {
        const math::Mat4 view = math::lookAt(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f);
        const math::Mat4 projection = math::perspective(60.0f * 3.14159265f / 180.0f, 16.0f / 9.0f, 0.05f, 650.0f);
        const math::Frustum f = math::Frustum::fromViewProjection(math::multiply(projection, view));
        expect(boxVisible(f, 0.0f, 0.0f, -10.0f, 1.0f), "a box straight ahead is visible");
        expect(!boxVisible(f, 0.0f, 0.0f, 10.0f, 1.0f), "a box behind the camera is culled");
        expect(!boxVisible(f, -40.0f, 0.0f, -10.0f, 1.0f), "a box far to the left is culled");
        expect(!boxVisible(f, 0.0f, 40.0f, -10.0f, 1.0f), "a box far above is culled");
        expect(!boxVisible(f, 0.0f, 0.0f, -700.0f, 1.0f), "a box past the far plane is culled");
        expect(boxVisible(f, -11.0f, 0.0f, -10.0f, 2.0f), "a box straddling the left edge is kept");
        expect(boxVisible(f, 0.0f, 0.0f, 0.0f, 0.5f), "a box around the camera is kept");
        const float ahead[3] = {0.0f, 0.0f, -20.0f};
        const float behind[3] = {0.0f, 0.0f, 20.0f};
        expect(f.intersectsSphere(ahead, 1.0f) && !f.intersectsSphere(behind, 1.0f), "sphere tests agree");
        // Plane normals are unit length, so distances are in metres.
        float worst = 0.0f;
        for (const auto& p : f.planes) {
            worst = std::fmax(worst, std::fabs(std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]) - 1.0f));
        }
        expect(worst < 1e-4f, "frustum planes are normalised");
    }

    // 2. Atmosphere through the day.
    {
        const renderer::Atmosphere noon = renderer::atmosphereAt(13.0f);
        const renderer::Atmosphere dusk = renderer::atmosphereAt(19.6f);
        const renderer::Atmosphere midnight = renderer::atmosphereAt(0.0f);
        const float len = std::sqrt(noon.sun_dir[0] * noon.sun_dir[0] + noon.sun_dir[1] * noon.sun_dir[1] + noon.sun_dir[2] * noon.sun_dir[2]);
        std::printf("atmosphere: noon sun height %.2f, night %.2f / %.2f / %.2f (noon, dusk, midnight)\n", noon.sun_dir[1], noon.night,
            dusk.night, midnight.night);
        expect(std::fabs(len - 1.0f) < 1e-4f, "the sun direction is a unit vector");
        expect(noon.sun_dir[1] > 0.7f, "the noon sun is high");
        expect(noon.night == 0.0f && midnight.night > 0.95f, "noon is day and midnight is night");
        expect(dusk.sky_horizon[0] > dusk.sky_horizon[2] + 0.1f, "dusk haze is warm");
        expect(midnight.sun_color[0] < 0.4f, "night light is dim");
        expect(noon.fog_density > 0.0f && renderer::atmosphereAt(13.0f, 0.0f).fog_density == 0.0f, "fog scales and can be turned off");
        expect(renderer::atmosphereAt(37.0f).sun_dir[1] == noon.sun_dir[1], "hours wrap around 24");
    }

    // 3. Dynamic quality.
    {
        using renderer::ResolutionScale;
        ResolutionScale r;
        ResolutionScale::Quality q{};
        // Slow start-up frames (shader compiles) are ignored.
        for (int i = 0; i < 20; ++i) q = r.update(0.07f, 1920, 1080);
        expect(r.level() == 0, "slow frames while the game warms up are ignored");
        for (int i = 0; i < 120; ++i) q = r.update(1.0f / 60.0f, 1920, 1080);
        expect(q.scale == 1.0f && q.msaa, "on-time frames at 1080p keep full resolution and MSAA");

        // Under load it walks down the ladder, and MSAA goes before the scale drops far.
        bool scale_low_with_msaa = false;
        for (int i = 0; i < 300; ++i) {
            q = r.update(0.033f, 1920, 1080);
            if (q.msaa && q.scale < 0.85f) scale_low_with_msaa = true;
        }
        const ResolutionScale::Quality dropped = q;
        for (int i = 0; i < 60 * 40; ++i) q = r.update(1.0f / 60.0f, 1920, 1080);
        std::printf("quality: %.2f scale, MSAA %s after 10 s at 30 fps; %.2f, MSAA %s after 40 s back at 60 fps\n", dropped.scale,
            dropped.msaa ? "on" : "off", q.scale, q.msaa ? "on" : "off");
        expect(dropped.scale == ResolutionScale::kMinScale && !dropped.msaa, "sustained late frames reach the lowest setting");
        expect(!scale_low_with_msaa, "MSAA is dropped before the resolution goes below 85%");
        expect(q.scale == 1.0f && q.msaa, "on-time frames climb back to full quality");

        ResolutionScale stalls;
        for (int i = 0; i < 20; ++i) q = stalls.update(0.5f, 1920, 1080);
        expect(q.scale == 1.0f && stalls.level() == 0, "stalls (tab switches) are ignored");

        // A hitch now and then does not stop the climb back.
        ResolutionScale hitchy;
        for (int i = 0; i < 300; ++i) q = hitchy.update(0.033f, 1920, 1080);
        for (int i = 0; i < 60 * 60; ++i) q = hitchy.update(i % 45 == 0 ? 0.04f : 1.0f / 60.0f, 1920, 1080);
        expect(q.scale == 1.0f && q.msaa, "occasional hitches still let quality recover");

        // A device that can only hold 60 fps without MSAA: it settles there instead of
        // bouncing back up to MSAA every few seconds.
        ResolutionScale weak;
        int changes = 0;
        bool last_msaa = true;
        int last_retry = 0;
        int last_gap = 0;
        bool gaps_grow = true;
        for (int i = 0; i < 60 * 180; ++i) {
            q = weak.update(q.msaa ? 0.025f : 1.0f / 60.0f, 1920, 1080);
            if (i > 0 && q.msaa != last_msaa) {
                ++changes;
                if (q.msaa) {  // a retry of MSAA
                    const int gap = i - last_retry;
                    if (last_retry > 0 && gap < last_gap) gaps_grow = false;
                    last_gap = last_retry > 0 ? gap : 0;
                    last_retry = i;
                }
            }
            last_msaa = q.msaa;
        }
        std::printf("quality: weak device changed MSAA %d times in 3 minutes, ends with MSAA %s\n", changes, q.msaa ? "on" : "off");
        expect(changes <= 12 && gaps_grow, "a level that is too slow is retried less and less often");

        ResolutionScale big;
        q = big.update(1.0f / 60.0f, 3840, 2160);
        std::printf("quality: 4K screen capped at %.2f\n", q.scale);
        expect(q.scale < 0.55f && q.scale > 0.45f, "a 4K screen is capped by the pixel budget");

        ResolutionScale fixed;
        fixed.setEnabled(false);
        fixed.setFixedScale(0.75f);
        for (int i = 0; i < 120; ++i) q = fixed.update(0.05f, 1280, 720);
        expect(q.scale == 0.75f && q.msaa, "with dynamic quality off, the fixed scale holds");
    }

    std::printf(failures == 0 ? "RENDER MATH OK\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
