#include "assets/StaticModel.h"

#include "cgltf.h"
#include "stb_image.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace assets {
namespace {

bool readFloats(const cgltf_accessor* accessor, size_t components, std::vector<float>& out) {
    out.assign(accessor->count * components, 0.0f);
    return cgltf_accessor_unpack_floats(accessor, out.data(), out.size()) != 0;
}

const cgltf_accessor* findAttribute(const cgltf_primitive& prim, cgltf_attribute_type type) {
    for (size_t i = 0; i < prim.attributes_count; ++i) {
        if (prim.attributes[i].type == type && prim.attributes[i].index == 0) {
            return prim.attributes[i].data;
        }
    }
    return nullptr;
}

} // namespace

bool StaticModel::load(const void* data, size_t size, float length) {
    mesh_ = StaticMesh{};
    cgltf_options options{};
    cgltf_data* gltf = nullptr;
    if (cgltf_parse(&options, data, size, &gltf) != cgltf_result_success) {
        return false;
    }
    if (cgltf_load_buffers(&options, gltf, nullptr) != cgltf_result_success || gltf->meshes_count == 0 ||
        gltf->meshes[0].primitives_count == 0) {
        cgltf_free(gltf);
        return false;
    }
    const cgltf_primitive& prim = gltf->meshes[0].primitives[0];
    const cgltf_accessor* position = findAttribute(prim, cgltf_attribute_type_position);
    if (position == nullptr) {
        cgltf_free(gltf);
        return false;
    }
    const cgltf_accessor* normal = findAttribute(prim, cgltf_attribute_type_normal);
    const cgltf_accessor* uv = findAttribute(prim, cgltf_attribute_type_texcoord);

    std::vector<float> pos, nrm, tex;
    readFloats(position, 3, pos);
    if (normal != nullptr) readFloats(normal, 3, nrm);
    if (uv != nullptr) readFloats(uv, 2, tex);

    const size_t count = position->count;
    mesh_.vertices.resize(count);
    for (size_t i = 0; i < count; ++i) {
        MeshVertex& v = mesh_.vertices[i];
        std::memcpy(v.position, &pos[i * 3], sizeof(v.position));
        if (normal != nullptr) {
            std::memcpy(v.normal, &nrm[i * 3], sizeof(v.normal));
        } else {
            v.normal[0] = 0.0f;
            v.normal[1] = 1.0f;
            v.normal[2] = 0.0f;
        }
        v.color[0] = v.color[1] = v.color[2] = v.color[3] = 1.0f;
        if (uv != nullptr) {
            std::memcpy(v.uv, &tex[i * 2], sizeof(v.uv));
        }
    }

    if (prim.indices != nullptr) {
        mesh_.indices.resize(prim.indices->count);
        for (size_t i = 0; i < prim.indices->count; ++i) {
            mesh_.indices[i] = static_cast<uint16_t>(cgltf_accessor_read_index(prim.indices, i));
        }
    } else {
        mesh_.indices.resize(count);
        for (size_t i = 0; i < count; ++i) {
            mesh_.indices[i] = static_cast<uint16_t>(i);
        }
    }

    if (prim.material != nullptr && prim.material->has_pbr_metallic_roughness) {
        const cgltf_texture* texture = prim.material->pbr_metallic_roughness.base_color_texture.texture;
        if (texture != nullptr && texture->image != nullptr && texture->image->buffer_view != nullptr) {
            const cgltf_buffer_view* view = texture->image->buffer_view;
            int width = 0, height = 0, channels = 0;
            stbi_uc* pixels = stbi_load_from_memory(cgltf_buffer_view_data(view), static_cast<int>(view->size),
                &width, &height, &channels, 4);
            if (pixels != nullptr) {
                mesh_.texture_width = width;
                mesh_.texture_height = height;
                mesh_.base_color_rgba.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
                stbi_image_free(pixels);
            }
        }
    }
    cgltf_free(gltf);

    // Normalize: centre the box, turn the longest side onto +Z, scale it to length.
    float lo[3] = {1e30f, 1e30f, 1e30f};
    float hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const MeshVertex& v : mesh_.vertices) {
        for (int c = 0; c < 3; ++c) {
            if (v.position[c] < lo[c]) lo[c] = v.position[c];
            if (v.position[c] > hi[c]) hi[c] = v.position[c];
        }
    }
    int longest = 0;
    for (int c = 1; c < 3; ++c) {
        if (hi[c] - lo[c] > hi[longest] - lo[longest]) longest = c;
    }
    const float extent = hi[longest] - lo[longest];
    if (extent <= 0.0f) {
        return false;
    }
    const float scale = length / extent;
    const float centre[3] = {(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
    // Each engine axis is a signed source axis, so the frame stays right-handed and the
    // faces keep their winding. The longest side becomes +Z. Up stays on the source's +Y
    // (glTF is Y-up), so a prop lies the way it was modelled. Only a prop longest along Y
    // uses a cyclic order, since its up is then not on Y.
    float basis[3][3] = {};  // basis[k] is engine axis k, in source coordinates
    if (longest == 1) {
        basis[0][2] = 1.0f;
        basis[1][0] = 1.0f;
        basis[2][1] = 1.0f;
    } else {
        basis[2][longest] = 1.0f;
        basis[1][1] = 1.0f;
        const float* y = basis[1];
        const float* z = basis[2];
        basis[0][0] = y[1] * z[2] - y[2] * z[1];
        basis[0][1] = y[2] * z[0] - y[0] * z[2];
        basis[0][2] = y[0] * z[1] - y[1] * z[0];
    }
    for (MeshVertex& v : mesh_.vertices) {
        const float p[3] = {(v.position[0] - centre[0]) * scale, (v.position[1] - centre[1]) * scale,
            (v.position[2] - centre[2]) * scale};
        const float n[3] = {v.normal[0], v.normal[1], v.normal[2]};
        for (int k = 0; k < 3; ++k) {
            v.position[k] = basis[k][0] * p[0] + basis[k][1] * p[1] + basis[k][2] * p[2];
            v.normal[k] = basis[k][0] * n[0] + basis[k][1] * n[1] + basis[k][2] * n[2];
        }
    }
    return true;
}

} // namespace assets
