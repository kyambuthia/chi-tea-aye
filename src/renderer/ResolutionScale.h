#pragma once

#include <cmath>

namespace renderer {

// Dynamic quality: picks the 3D render scale and whether to multisample from measured
// frame times, so a slow device drops pixels instead of frames. The UI is always drawn
// at full resolution.
//
// It walks a ladder of settings: a small drop in resolution first, then MSAA off at
// full resolution (on weak integrated GPUs MSAA costs more than pixels do), then lower
// resolutions. It steps down quickly when frames run long and climbs back slowly when
// they are comfortably on time, with a hold after each drop. A level that proved too
// slow waits twice as long before it is tried again, so it does not oscillate.
// A pixel budget also caps the scale on very large or very dense screens.
class ResolutionScale {
public:
    struct Quality {
        float scale;
        bool msaa;
    };

    static constexpr float kMinScale = 0.5f;
    static constexpr float kTargetFrame = 1.0f / 60.0f;
    static constexpr float kSlowFrame = 1.25f * kTargetFrame;   // above this, frames are late
    static constexpr float kFastFrame = 1.08f * kTargetFrame;   // below this, there is headroom
    static constexpr float kSlowTimeToDrop = 0.5f;   // seconds of late frames before a step down
    static constexpr float kFastTimeToRise = 2.0f;   // seconds of on-time frames before a step up
    static constexpr float kHoldAfterDrop = 3.0f;
    static constexpr float kStallFrame = 0.25f;      // longer frames are stalls (tab switch): ignored
    static constexpr float kPixelBudget = 2.2e6f;    // about 2048 x 1080
    static constexpr float kWarmUp = 1.5f;           // first seconds ignored: shaders compile, assets settle

    static constexpr int kLevels = 7;
    static constexpr Quality kLadder[kLevels] = {
        {1.0f, true}, {0.85f, true}, {1.0f, false}, {0.85f, false}, {0.7f, false}, {0.6f, false}, {0.5f, false},
    };

    // Call once a frame with the frame's duration.
    Quality update(float frame_seconds, int framebuffer_width, int framebuffer_height) {
        if (warm_up_ < kWarmUp) {
            warm_up_ += std::fmax(frame_seconds, 0.0f);
        } else if (frame_seconds > 0.0f && frame_seconds < kStallFrame) {
            // A short average smooths single hitches.
            average_ += (frame_seconds - average_) * 0.1f;
            hold_ = std::fmax(0.0f, hold_ - frame_seconds);
            if (average_ > kSlowFrame) {
                slow_ += frame_seconds;
                fast_ = 0.0f;
            } else if (average_ < kFastFrame) {
                fast_ += frame_seconds;
                slow_ = 0.0f;
            } else {
                // In between: neither late nor clearly early. A brief hitch should not
                // throw away the time already spent on time, so only the slow count resets.
                slow_ = 0.0f;
            }
            if (enabled_ && slow_ > kSlowTimeToDrop && level_ < kLevels - 1) {
                // This level was too slow: it takes twice as long to be tried again.
                failures_[level_] = failures_[level_] < 5 ? failures_[level_] + 1 : 5;
                ++level_;
                slow_ = 0.0f;
                hold_ = kHoldAfterDrop;
                average_ = kTargetFrame;  // let the new setting prove itself
            } else if (enabled_ && level_ > 0 && hold_ <= 0.0f &&
                fast_ > kFastTimeToRise * static_cast<float>(1 << failures_[level_ - 1])) {
                --level_;
                fast_ = 0.0f;
            }
        }
        const float pixels = static_cast<float>(framebuffer_width) * static_cast<float>(framebuffer_height);
        const float cap = pixels > kPixelBudget ? std::sqrt(kPixelBudget / pixels) : 1.0f;
        const Quality wanted = enabled_ ? kLadder[level_] : Quality{fixed_, true};
        return Quality{std::fmax(kMinScale * cap, std::fmin(wanted.scale, cap)), wanted.msaa};
    }

    // Off: render at the fixed scale (still under the pixel budget), MSAA as configured.
    void setEnabled(bool enabled) { enabled_ = enabled; }
    void setFixedScale(float scale) { fixed_ = scale; }
    bool enabled() const { return enabled_; }
    int level() const { return level_; }
    float averageFrame() const { return average_; }

private:
    int level_ = 0;
    int failures_[kLevels] = {};  // times each level proved too slow
    float warm_up_ = 0.0f;
    float fixed_ = 1.0f;
    float average_ = kTargetFrame;
    float slow_ = 0.0f;
    float fast_ = 0.0f;
    float hold_ = 0.0f;
    bool enabled_ = true;
};

} // namespace renderer
