#pragma once

namespace assets {

// Vertex layout shared by static scenery and skinned characters. Static
// geometry sets zero joint weights so the skinning path is skipped.
struct MeshVertex {
    float position[3];
    float normal[3];
    float color[4];
    float uv[2];
    float joints[4];
    float weights[4];
};

} // namespace assets
