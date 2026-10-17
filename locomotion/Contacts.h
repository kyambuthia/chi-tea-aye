#pragma once

#include <vector>

// Foot contact detection and stride segmentation, from a foot's height above
// the ground and its speed, sampled at a fixed rate.
namespace locomotion {

struct ContactParams {
    float enter_height = 0.03f;   // meters above the ground: below this and slow, the foot is planted
    float exit_height = 0.05f;    // hysteresis: stays planted until above this or fast
    float enter_speed = 0.25f;    // meters per second
    float exit_speed = 0.35f;
    int min_frames = 3;           // shorter contacts are noise
};

// A planted interval, [begin, end) in sample indices.
struct Contact {
    int begin = 0;
    int end = 0;
};

// A stride: the foot lifts at liftoff (the end of one contact) and lands at
// plant (the start of the next).
struct Stride {
    int liftoff = 0;
    int plant = 0;
};

std::vector<Contact> detectContacts(const std::vector<float>& height, const std::vector<float>& speed,
    const ContactParams& params);

std::vector<Stride> strides(const std::vector<Contact>& contacts);

} // namespace locomotion
