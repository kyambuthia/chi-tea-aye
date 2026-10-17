#include "locomotion/Contacts.h"

namespace locomotion {

std::vector<Contact> detectContacts(const std::vector<float>& height, const std::vector<float>& speed,
    const ContactParams& params) {
    std::vector<Contact> contacts;
    bool planted = false;
    int begin = 0;
    const int n = static_cast<int>(height.size());
    for (int i = 0; i < n; ++i) {
        // Hysteresis: a planted foot stays planted until it clearly lifts or slides.
        if (!planted) {
            if (height[i] < params.enter_height && speed[i] < params.enter_speed) {
                planted = true;
                begin = i;
            }
        } else if (height[i] > params.exit_height || speed[i] > params.exit_speed) {
            planted = false;
            if (i - begin >= params.min_frames) {
                contacts.push_back({begin, i});
            }
        }
    }
    if (planted && n - begin >= params.min_frames) {
        contacts.push_back({begin, n});
    }
    return contacts;
}

std::vector<Stride> strides(const std::vector<Contact>& contacts) {
    std::vector<Stride> out;
    for (size_t i = 0; i + 1 < contacts.size(); ++i) {
        out.push_back({contacts[i].end, contacts[i + 1].begin});
    }
    return out;
}

} // namespace locomotion
