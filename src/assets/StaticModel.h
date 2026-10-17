#pragma once

#include "assets/MeshVertex.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace assets {

// A static, unskinned prop (a weapon). It is normalized on load: scaled so its
// longest side is the requested length, centred on its box, and turned so the
// longest side runs along +Z, which is the barrel direction.
struct StaticMesh {
    std::vector<MeshVertex> vertices;  // joints and weights are zero: no skinning
    std::vector<uint16_t> indices;
    std::vector<uint8_t> base_color_rgba;
    int texture_width = 0;
    int texture_height = 0;
};

class StaticModel {
public:
    // Loads the first primitive of the first mesh in a GLB. Returns false when
    // the file has no usable geometry or cannot be parsed.
    bool load(const void* data, size_t size, float length);

    const StaticMesh& mesh() const { return mesh_; }

private:
    StaticMesh mesh_;
};

} // namespace assets
