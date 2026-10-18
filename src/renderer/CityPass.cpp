#include "renderer/CityPass.h"

#include "gameplay/GameState.h"
#include "math/Vec3.h"
#include "physics/Vehicle.h"
#include "renderer/GlUtil.h"
#include "renderer/ShaderChunks.h"
#include "world/City.h"
#include "world/World.h"

#include <cmath>
#include <cstddef>
#include <string>

namespace renderer {
namespace {

// Box kinds, stored in Instance::color[3]; the fragment shader shades each its own way.
enum Kind : int {
    kBuilding = 0,
    kPlain = 1,
    kPaint = 2,
    kGlass = 3,
    kTyre = 4,
    kLampHead = 5,
    kHeadlight = 6,
    kTaillight = 7,
};

constexpr GLuint kAttrModel0 = 2;  // four columns: 2, 3, 4, 5
constexpr GLuint kAttrInstanceColor = 6;
constexpr GLuint kAttrInstanceParams = 7;

constexpr float kCarPaints[][3] = {
    {0.55f, 0.06f, 0.05f}, {0.82f, 0.82f, 0.80f}, {0.05f, 0.05f, 0.06f}, {0.50f, 0.52f, 0.55f},
    {0.08f, 0.18f, 0.42f}, {0.86f, 0.64f, 0.08f}, {0.10f, 0.28f, 0.16f}, {0.32f, 0.07f, 0.10f},
};
constexpr int kCarPaintCount = static_cast<int>(sizeof(kCarPaints) / sizeof(kCarPaints[0]));

std::string boxVertexSource() {
    return std::string(CHI_GLSL_HEADER) + R"(
        uniform mat4 view_projection;
        in vec3 position;
        in vec3 normal;
        in vec4 i_m0;
        in vec4 i_m1;
        in vec4 i_m2;
        in vec4 i_m3;
        in vec4 i_color;
        in vec4 i_params;
        out vec3 v_world;
        out vec3 v_normal;
        out vec3 v_local;
        out vec3 v_size;
        out vec3 v_obj_n;
        out vec4 v_color;
        out vec4 v_params;

        void main() {
            mat4 m = mat4(i_m0, i_m1, i_m2, i_m3);
            vec3 s = vec3(length(i_m0.xyz), length(i_m1.xyz), length(i_m2.xyz));
            vec4 w = m * vec4(position, 1.0);
            v_world = w.xyz;
            // (R S) (n / s^2) = R (n / s): the inverse-transpose for rotation times scale.
            v_normal = normalize(mat3(m) * (normal / (s * s)));
            v_local = position * s;
            v_size = s;
            v_obj_n = normal;
            v_color = i_color;
            v_params = i_params;
            gl_Position = view_projection * w;
        }
    )";
}

std::string boxFragmentSource() {
    return std::string(CHI_GLSL_HEADER) + kAtmosphereGlsl + R"(
        in vec3 v_world;
        in vec3 v_normal;
        in vec3 v_local;
        in vec3 v_size;
        in vec3 v_obj_n;
        in vec4 v_color;
        in vec4 v_params;
        out vec4 frag_color;

        void main() {
            vec3 n = normalize(v_normal);
            vec3 v = normalize(eye_pos - v_world);
            int kind = int(v_color.w + 0.5);
            float night = fog_params.y;
            vec3 albedo = v_color.rgb;
            float ndl = max(dot(n, sun_dir), 0.0);
            float fresnel = pow(1.0 - max(dot(n, v), 0.0), 4.0);
            vec3 reflection = mix(amb_sky, fog_color, 0.5);
            vec3 h = normalize(sun_dir + v);
            float ao = 1.0;
            float spec = 0.0;
            float shine = 24.0;
            vec3 emissive = vec3(0.0);
            float glass = 0.0;  // 0 wall, 1 window glass

            if (kind == 0) {
                // Buildings: floors 3.2 m tall, bays along each face, shop windows at street level.
                float height = v_world.y - v_params.y;
                ao = mix(0.55, 1.0, clamp(height / 6.0, 0.0, 1.0));
                if (abs(v_obj_n.y) < 0.5) {
                    bool x_face = abs(v_obj_n.x) > 0.5;
                    float u = x_face ? v_local.z : v_local.x;
                    float half_width = 0.5 * (x_face ? v_size.z : v_size.x);
                    float bay = 2.4 + 1.2 * v_params.x;
                    vec2 cell = vec2((u + half_width) / bay, height / 3.2);
                    vec2 f = fract(cell);
                    vec2 id = floor(cell);
                    bool street = cell.y < 1.0;
                    vec2 lo = street ? vec2(0.05, 0.10) : vec2(0.20, 0.30);
                    vec2 hi = street ? vec2(0.95, 0.80) : vec2(0.80, 0.84);
                    vec2 aa = fwidth(cell) * 1.2;
                    vec2 inside = smoothstep(lo - aa, lo + aa, f) * (1.0 - smoothstep(hi - aa, hi + aa, f));
                    glass = inside.x * inside.y;
                    // No windows on the corners, the parapet, or below the ground.
                    if (abs(u) > half_width - 0.5 || v_local.y > 0.5 * v_size.y - 0.7 || height < 0.3) glass = 0.0;
                    float r = hash12(id + vec2(v_params.x * 131.0, v_obj_n.x * 7.0 + v_obj_n.z * 17.0));
                    float lit = step(0.62 - 0.3 * v_params.x, r) * night;
                    // Mostly warm tungsten, some cool fluorescent; shop fronts a little brighter.
                    vec3 warm = mix(vec3(0.95, 0.66, 0.34), vec3(0.62, 0.68, 0.76), step(0.85, r));
                    emissive = warm * lit * glass * (0.45 + 0.4 * fract(r * 13.0)) * (street ? 1.15 : 1.0);
                    // Faint horizontal band between floors breaks up big flat walls.
                    float band = 1.0 - smoothstep(0.0, 0.06, abs(f.y - 0.06)) * 0.12;
                    albedo *= band;
                } else {
                    albedo *= 0.5;  // roof: tar and gravel
                }
            } else if (kind == 2) {
                spec = 0.8;
                shine = 64.0;
                glass = 0.0;
                albedo += reflection * fresnel * 0.5;
            } else if (kind == 3) {
                glass = 1.0;
            } else if (kind == 4) {
                albedo = vec3(0.03);
            } else if (kind == 5) {
                albedo = vec3(0.25);
                emissive = vec3(1.0, 0.70, 0.36) * (0.15 + 2.2 * night);
            } else if (kind == 6) {
                albedo = vec3(0.3);
                emissive = vec3(1.0, 0.96, 0.85) * (0.35 + 1.6 * night);
            } else if (kind == 7) {
                albedo = vec3(0.12, 0.01, 0.01);
                emissive = vec3(0.9, 0.05, 0.03) * (0.4 + 1.2 * night);
            }

            vec3 wall = albedo * (sun_color * ndl * 0.9 + hemiAmbient(n)) * ao;
            wall += sun_color * spec * pow(max(dot(n, h), 0.0), shine) * ndl;
            vec3 glass_color = mix(vec3(0.03, 0.04, 0.05), reflection, 0.18 + 0.7 * fresnel);
            glass_color += sun_color * pow(max(dot(n, h), 0.0), 90.0) * ndl * 1.5;
            vec3 color = mix(wall, glass_color, glass) + emissive;
            frag_color = vec4(applyFog(color, v_world), 1.0);
        }
    )";
}

std::string floorVertexSource() {
    return std::string(CHI_GLSL_HEADER) + R"(
        uniform mat4 view_projection;
        in vec3 position;
        out vec3 v_world;
        void main() {
            v_world = position;
            gl_Position = view_projection * vec4(position, 1.0);
        }
    )";
}

// Streets, sidewalks and lots from the world position alone, mirroring world::surfaceAt.
std::string floorFragmentSource() {
    return std::string(CHI_GLSL_HEADER) + kAtmosphereGlsl + R"(
        in vec3 v_world;
        out vec4 frag_color;

        float localCoord(float v) { return v - 60.0 * floor(v / 60.0 + 0.5); }
        float roadCentre(float v) { return 30.0 + 60.0 * floor((v - 30.0) / 60.0 + 0.5); }
        float lineMask(float d, float centre, float half_width) {
            float fw = max(fwidth(d), 1e-4);
            return 1.0 - smoothstep(half_width - fw, half_width + fw, abs(d - centre));
        }

        // Road markings for a road running along 'along', with 'across' measured from its centreline.
        vec3 markings(vec3 c, float across, float along, float other_local) {
            float d = abs(across);
            // Zebra crossing just before the junction.
            if (other_local > 19.4 && other_local < 21.9 && d < 7.4) {
                float stripe = step(0.45, fract(across / 1.1));
                return mix(c, vec3(0.78), stripe * 0.9);
            }
            float yellow = lineMask(d, 0.18, 0.08);
            float dash = lineMask(d, 4.0, 0.08) * step(0.5, fract(along / 6.0));
            float edge = lineMask(d, 7.55, 0.1);
            c = mix(c, vec3(0.78, 0.62, 0.16), yellow);
            c = mix(c, vec3(0.80), max(dash, edge));
            // Darker tyre tracks in the middle of each lane.
            float lane = abs(fract(d / 4.0) - 0.5);
            c *= 1.0 - 0.06 * (1.0 - smoothstep(0.1, 0.3, lane));
            return c;
        }

        void main() {
            vec2 p = v_world.xz;
            float lx = abs(localCoord(p.x));
            float lz = abs(localCoord(p.y));
            float grain = groundGrain(p);
            vec3 c;
            float wet = 0.0;  // asphalt picks up a little sheen
            if (max(abs(p.x), abs(p.y)) > 218.0) {
                c = vec3(0.16, 0.15, 0.13) + grain;  // past the ring road: waste ground
            } else if (lx > 22.0 || lz > 22.0) {
                c = vec3(0.12, 0.12, 0.125) + grain;
                wet = 1.0;
                if (lx > 22.0 && lz <= 22.0) {
                    c = markings(c, p.x - roadCentre(p.x), p.y, lz);
                } else if (lz > 22.0 && lx <= 22.0) {
                    c = markings(c, p.y - roadCentre(p.y), p.x, lx);
                }
            } else if (lx > 19.0 || lz > 19.0) {
                c = vec3(0.40, 0.39, 0.37) + grain;
                vec2 t = fract(p / 1.5);
                float fw = max(fwidth(p.x / 1.5), 1e-4) * 1.5;
                float seam = 1.0 - smoothstep(0.0, 0.02 + fw, min(min(t.x, 1.0 - t.x), min(t.y, 1.0 - t.y)));
                c *= 1.0 - 0.18 * seam;
                if (max(lx, lz) > 21.75) c = vec3(0.52, 0.51, 0.48);  // curb stone
            } else {
                c = vec3(0.19, 0.19, 0.18) + grain;  // lots and courtyards
            }
            vec3 n = vec3(0.0, 1.0, 0.0);
            vec3 color = c * (sun_color * max(sun_dir.y, 0.0) * 0.9 + hemiAmbient(n));
            vec3 v = normalize(eye_pos - v_world);
            float fresnel = pow(1.0 - max(v.y, 0.0), 5.0);
            color += wet * fresnel * fog_color * 0.25;
            color += c * vec3(1.0, 0.62, 0.30) * lampPools(p) * fog_params.y * 2.2;
            frag_color = vec4(applyFog(color, v_world), 1.0);
        }
    )";
}

std::string skyVertexSource() {
    return std::string(CHI_GLSL_HEADER) + R"(
        in vec2 position;
        out vec2 v_ndc;
        void main() {
            v_ndc = position;
            gl_Position = vec4(position, 1.0, 1.0);  // on the far plane
        }
    )";
}

std::string skyFragmentSource() {
    return std::string(CHI_GLSL_HEADER) + kAtmosphereGlsl + R"(
        uniform mat4 basis;  // columns: right, up, forward; [3].xy = tan(fov/2) * aspect, tan(fov/2)
        uniform vec3 sky_zenith;
        in vec2 v_ndc;
        out vec4 frag_color;
        void main() {
            vec3 dir = normalize(basis[2].xyz + v_ndc.x * basis[3].x * basis[0].xyz + v_ndc.y * basis[3].y * basis[1].xyz);
            float t = clamp(dir.y, 0.0, 1.0);
            vec3 c = mix(fog_color, sky_zenith, pow(t, 0.5));
            if (dir.y < 0.0) c = fog_color * (1.0 + dir.y * 0.3);
            float s = max(dot(dir, sun_dir), 0.0);
            float day = 1.0 - fog_params.y;
            c += sun_color * (pow(s, 900.0) * 6.0 + pow(s, 12.0) * 0.22) * day;
            c += vec3(0.8, 0.85, 1.0) * pow(s, 3000.0) * 3.0 * fog_params.y;  // the moon
            frag_color = vec4(c, 1.0);
        }
    )";
}

std::string shadowVertexSource() {
    return std::string(CHI_GLSL_HEADER) + R"(
        uniform mat4 mvp;
        in vec2 position;
        out vec2 v_p;
        void main() {
            v_p = position;
            gl_Position = mvp * vec4(position.x, 0.0, position.y, 1.0);
        }
    )";
}

std::string shadowFragmentSource() {
    return std::string(CHI_GLSL_HEADER) + R"(
        uniform vec4 shape;  // half sizes x, z (metres), softness (metres), strength
        in vec2 v_p;
        out vec4 frag_color;
        void main() {
            // Rounded-rectangle distance in metres; the quad is the shape plus its softness.
            vec2 extent = shape.xy + shape.z;
            vec2 q = abs(v_p * extent) - shape.xy + shape.z;
            float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - shape.z;
            float a = (1.0 - smoothstep(-shape.z, shape.z, d)) * shape.w;
            frag_color = vec4(0.0, 0.0, 0.0, a);
        }
    )";
}

// Unit cube centred on the origin: position and normal per vertex.
void buildCube(std::vector<float>& vertices, std::vector<uint16_t>& indices) {
    for (int axis = 0; axis < 3; ++axis) {
        for (const float sign : {-1.0f, 1.0f}) {
            const int next = (axis + 1) % 3;
            const int after = (axis + 2) % 3;
            const int u_axis = sign > 0.0f ? next : after;
            const int v_axis = sign > 0.0f ? after : next;
            const uint16_t base = static_cast<uint16_t>(vertices.size() / 6);
            for (int k = 0; k < 4; ++k) {
                float p[3] = {0.0f, 0.0f, 0.0f};
                float n[3] = {0.0f, 0.0f, 0.0f};
                p[axis] = 0.5f * sign;
                p[u_axis] = (k == 1 || k == 2) ? 0.5f : -0.5f;
                p[v_axis] = k >= 2 ? 0.5f : -0.5f;
                n[axis] = sign;
                vertices.insert(vertices.end(), {p[0], p[1], p[2], n[0], n[1], n[2]});
            }
            indices.insert(indices.end(), {base, static_cast<uint16_t>(base + 1), static_cast<uint16_t>(base + 2), base,
                static_cast<uint16_t>(base + 2), static_cast<uint16_t>(base + 3)});
        }
    }
}

CityPass::Instance makeInstance(const math::Mat4& model, const float color[3], int kind, float seed = 0.0f, float base = 0.0f) {
    CityPass::Instance inst{};
    for (int i = 0; i < 16; ++i) inst.model[i] = model.m[i];
    inst.color[0] = color[0];
    inst.color[1] = color[1];
    inst.color[2] = color[2];
    inst.color[3] = static_cast<float>(kind);
    inst.params[0] = seed;
    inst.params[1] = base;
    return inst;
}

math::Mat4 boxModel(const float lo[3], const float hi[3]) {
    const float centre[3] = {0.5f * (lo[0] + hi[0]), 0.5f * (lo[1] + hi[1]), 0.5f * (lo[2] + hi[2])};
    const float size[3] = {hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]};
    const float no_rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    return math::fromTrs(centre, no_rotation, size);
}

// A part of a car: a box centred at c (metres, in the body box's frame) with size s.
math::Mat4 part(const math::Mat4& frame, float cx, float cy, float cz, float sx, float sy, float sz) {
    const float c[3] = {cx, cy, cz};
    const float s[3] = {sx, sy, sz};
    const float no_rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    return math::multiply(frame, math::fromTrs(c, no_rotation, s));
}

} // namespace

struct CityPass::Locations {
    AtmosphereUniforms box;
    AtmosphereUniforms floor;
    AtmosphereUniforms sky;
    GLint sky_zenith = -1;
};

bool CityPass::init(const world::World& world) {
    world_ = &world;
    locations_ = new Locations{};

    // Static boxes: practice walls, buildings, and street lamps (pole and head).
    const auto& boxes = world.walls();
    const auto& styles = world.wallStyles();
    for (size_t i = 0; i < boxes.size(); ++i) {
        StaticInstance s{};
        const world::Box& b = boxes[i];
        const int kind = styles[i].building ? kBuilding : kPlain;
        s.instance = makeInstance(boxModel(b.min, b.max), styles[i].color, kind, styles[i].seed, world::World::kBaseHeight);
        for (int c = 0; c < 3; ++c) {
            s.lo[c] = b.min[c];
            s.hi[c] = b.max[c];
        }
        static_instances_.push_back(s);
    }
    const float pole_color[3] = {0.16f, 0.17f, 0.18f};
    const float lamp_color[3] = {0.9f, 0.85f, 0.7f};
    for (const world::Box& pole : world.city().lamps()) {
        StaticInstance s{};
        s.instance = makeInstance(boxModel(pole.min, pole.max), pole_color, kPlain);
        for (int c = 0; c < 3; ++c) {
            s.lo[c] = pole.min[c];
            s.hi[c] = pole.max[c];
        }
        static_instances_.push_back(s);
        const float cx = 0.5f * (pole.min[0] + pole.max[0]);
        const float cz = 0.5f * (pole.min[2] + pole.max[2]);
        const float head_lo[3] = {cx - 0.28f, pole.max[1] - 0.05f, cz - 0.28f};
        const float head_hi[3] = {cx + 0.28f, pole.max[1] + 0.12f, cz + 0.28f};
        StaticInstance h{};
        h.instance = makeInstance(boxModel(head_lo, head_hi), lamp_color, kLampHead);
        for (int c = 0; c < 3; ++c) {
            h.lo[c] = head_lo[c];
            h.hi[c] = head_hi[c];
        }
        static_instances_.push_back(h);
    }

    box_program_ = buildProgram(boxVertexSource(), boxFragmentSource(), "city boxes",
        {{0, "position"}, {1, "normal"}, {kAttrModel0, "i_m0"}, {kAttrModel0 + 1, "i_m1"}, {kAttrModel0 + 2, "i_m2"},
            {kAttrModel0 + 3, "i_m3"}, {kAttrInstanceColor, "i_color"}, {kAttrInstanceParams, "i_params"}});
    floor_program_ = buildProgram(floorVertexSource(), floorFragmentSource(), "city floor", {{0, "position"}});
    sky_program_ = buildProgram(skyVertexSource(), skyFragmentSource(), "sky", {{0, "position"}});
    shadow_program_ = buildProgram(shadowVertexSource(), shadowFragmentSource(), "blob shadow", {{0, "position"}});
    if (box_program_ == 0 || floor_program_ == 0 || sky_program_ == 0 || shadow_program_ == 0) {
        return false;
    }
    box_vp_ = glGetUniformLocation(box_program_, "view_projection");
    floor_vp_ = glGetUniformLocation(floor_program_, "view_projection");
    sky_basis_ = glGetUniformLocation(sky_program_, "basis");
    shadow_mvp_ = glGetUniformLocation(shadow_program_, "mvp");
    shadow_shape_ = glGetUniformLocation(shadow_program_, "shape");
    locations_->box.locate(box_program_);
    locations_->floor.locate(floor_program_);
    locations_->sky.locate(sky_program_);
    locations_->sky_zenith = glGetUniformLocation(sky_program_, "sky_zenith");

    // Instanced boxes: the cube per vertex, the instance data per instance.
    std::vector<float> cube;
    std::vector<uint16_t> cube_indices;
    buildCube(cube, cube_indices);
    glGenVertexArrays(1, &box_vao_);
    glBindVertexArray(box_vao_);
    glGenBuffers(1, &box_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, box_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(cube.size() * sizeof(float)), cube.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));
    glGenBuffers(1, &box_ibo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, box_ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(cube_indices.size() * sizeof(uint16_t)), cube_indices.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &instance_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
    instance_capacity_ = static_instances_.size() + 256;
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(instance_capacity_ * sizeof(Instance)), nullptr, GL_DYNAMIC_DRAW);
    const GLsizei stride = sizeof(Instance);
    for (GLuint c = 0; c < 4; ++c) {
        glEnableVertexAttribArray(kAttrModel0 + c);
        glVertexAttribPointer(kAttrModel0 + c, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Instance, model) + c * 4 * sizeof(float)));
        glVertexAttribDivisor(kAttrModel0 + c, 1);
    }
    glEnableVertexAttribArray(kAttrInstanceColor);
    glVertexAttribPointer(kAttrInstanceColor, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Instance, color)));
    glVertexAttribDivisor(kAttrInstanceColor, 1);
    glEnableVertexAttribArray(kAttrInstanceParams);
    glVertexAttribPointer(kAttrInstanceParams, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Instance, params)));
    glVertexAttribDivisor(kAttrInstanceParams, 1);
    glBindVertexArray(0);

    // The city floor: four slabs around the park's terrain mesh, at street level.
    const float in = world::World::kHalfSize;
    const float out = world::World::kCityHalfSize + 80.0f;  // past the ring road, into the haze
    const float y = world::World::kBaseHeight;
    const float rects[4][4] = {{-out, -out, out, -in}, {-out, in, out, out}, {-out, -in, -in, in}, {in, -in, out, in}};
    std::vector<float> floor;
    for (const auto& r : rects) {
        // Two triangles, counter-clockwise seen from above.
        const float x0 = r[0], z0 = r[1], x1 = r[2], z1 = r[3];
        floor.insert(floor.end(), {x0, y, z0, x0, y, z1, x1, y, z1, x0, y, z0, x1, y, z1, x1, y, z0});
    }
    floor_vertex_count_ = static_cast<GLsizei>(floor.size() / 3);
    glGenVertexArrays(1, &floor_vao_);
    glBindVertexArray(floor_vao_);
    glGenBuffers(1, &floor_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, floor_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(floor.size() * sizeof(float)), floor.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);

    // Sky: one full-screen triangle. Shadows: a unit quad in [-1, 1]^2.
    const float tri[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    glGenVertexArrays(1, &sky_vao_);
    glBindVertexArray(sky_vao_);
    glGenBuffers(1, &sky_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, sky_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    const float quad[8] = {-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f};
    glGenVertexArrays(1, &shadow_vao_);
    glBindVertexArray(shadow_vao_);
    glGenBuffers(1, &shadow_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, shadow_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
    return true;
}

void CityPass::appendCar(const gameplay::GameState& gameplay, int index, std::vector<Instance>& out) const {
    const physics::Vehicle& car = gameplay.vehicles()[index];
    const math::Mat4 body = car.bodyTransform();
    const float* paint = kCarPaints[index % kCarPaintCount];
    const float trim[3] = {0.06f, 0.06f, 0.07f};
    const float glass[3] = {0.1f, 0.12f, 0.14f};
    const float light[3] = {1.0f, 1.0f, 1.0f};
    const math::Vec3 he = car.spec().half_extents;
    const float w = 2.0f * he.x;
    const float l = 2.0f * he.z;
    out.push_back(makeInstance(part(body, 0.0f, -0.14f, 0.0f, w, 0.62f, l), paint, kPaint));
    out.push_back(makeInstance(part(body, 0.0f, 0.36f, -0.25f, w * 0.88f, 0.44f, l * 0.5f), glass, kGlass));
    out.push_back(makeInstance(part(body, 0.0f, 0.59f, -0.3f, w * 0.84f, 0.05f, l * 0.44f), paint, kPaint));
    out.push_back(makeInstance(part(body, 0.0f, -0.32f, he.z + 0.02f, w * 0.96f, 0.2f, 0.08f), trim, kPlain));
    out.push_back(makeInstance(part(body, 0.0f, -0.32f, -he.z - 0.02f, w * 0.96f, 0.2f, 0.08f), trim, kPlain));
    for (const float side : {-1.0f, 1.0f}) {
        out.push_back(makeInstance(part(body, side * he.x * 0.66f, -0.06f, he.z + 0.01f, 0.34f, 0.13f, 0.04f), light, kHeadlight));
        out.push_back(makeInstance(part(body, side * he.x * 0.7f, -0.04f, -he.z - 0.01f, 0.3f, 0.12f, 0.04f), light, kTaillight));
    }
    const float tyre[3] = {0.03f, 0.03f, 0.03f};
    const float d = 2.0f * car.spec().wheel_radius;
    for (int i = 0; i < 4; ++i) {
        const float s[3] = {0.24f, d, d};
        const float c[3] = {0.0f, 0.0f, 0.0f};
        const float no_rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        out.push_back(makeInstance(math::multiply(car.wheelTransform(i), math::fromTrs(c, no_rotation, s)), tyre, kTyre));
    }
}

void CityPass::drawOpaque(const CityFrame& frame, const gameplay::GameState& gameplay) {
    if (box_program_ == 0) {
        return;
    }
    if (!world_->cityEnabled()) {
        visible_instances_ = 0;
        car_instances_ = 0;
        return;
    }
    // Cull: outside the frustum, or so far that the fog has swallowed it.
    frame_instances_.clear();
    const float max_d2 = frame.max_distance * frame.max_distance;
    for (const StaticInstance& s : static_instances_) {
        const float dx = std::fmax(0.0f, std::fmax(s.lo[0] - frame.eye[0], frame.eye[0] - s.hi[0]));
        const float dz = std::fmax(0.0f, std::fmax(s.lo[2] - frame.eye[2], frame.eye[2] - s.hi[2]));
        if (dx * dx + dz * dz > max_d2 || !frame.frustum.intersectsBox(s.lo, s.hi)) {
            continue;
        }
        frame_instances_.push_back(s.instance);
    }
    const size_t before_cars = frame_instances_.size();
    for (int i = 0; i < static_cast<int>(gameplay.vehicles().size()); ++i) {
        const math::Vec3 p = gameplay.vehicles()[i].renderPosition();
        const float c[3] = {p.x, p.y, p.z};
        if (frame.frustum.intersectsSphere(c, 3.0f)) {
            appendCar(gameplay, i, frame_instances_);
        }
    }
    car_instances_ = static_cast<int>(frame_instances_.size() - before_cars);
    visible_instances_ = static_cast<int>(frame_instances_.size());

    // Floor first: it covers most of the screen and fills the depth buffer early.
    glUseProgram(floor_program_);
    glUniformMatrix4fv(floor_vp_, 1, GL_FALSE, frame.view_projection.m);
    locations_->floor.set(frame.atmosphere, frame.eye);
    glBindVertexArray(floor_vao_);
    glDrawArrays(GL_TRIANGLES, 0, floor_vertex_count_);

    if (frame_instances_.empty()) {
        glBindVertexArray(0);
        return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
    if (frame_instances_.size() > instance_capacity_) {
        instance_capacity_ = frame_instances_.size() * 2;
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(instance_capacity_ * sizeof(Instance)), nullptr, GL_DYNAMIC_DRAW);
    } else {
        // Orphan the old storage so the driver need not wait on last frame's draw.
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(instance_capacity_ * sizeof(Instance)), nullptr, GL_DYNAMIC_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(frame_instances_.size() * sizeof(Instance)), frame_instances_.data());

    glUseProgram(box_program_);
    glUniformMatrix4fv(box_vp_, 1, GL_FALSE, frame.view_projection.m);
    locations_->box.set(frame.atmosphere, frame.eye);
    glBindVertexArray(box_vao_);
    glDrawElementsInstanced(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, nullptr, static_cast<GLsizei>(frame_instances_.size()));
    glBindVertexArray(0);
}

void CityPass::drawShadows(const CityFrame& frame, const gameplay::GameState& gameplay, const float warrior[3]) {
    if (shadow_program_ == 0) {
        return;
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.0f, -2.0f);
    glUseProgram(shadow_program_);
    glBindVertexArray(shadow_vao_);
    const float strength = 0.55f - 0.2f * frame.atmosphere.night;

    auto draw = [&](float x, float z, float yaw, float half_x, float half_z, float soft, float alpha) {
        // Lie on the ground, turned to the yaw, scaled so the quad covers shape plus softness.
        const math::Mat4 ground = world_->groundFrame(x, z);
        const math::Mat4 placed = math::multiply(math::multiply(ground, math::translation(0.0f, 0.02f, 0.0f)), math::rotationY(yaw));
        const float c[3] = {0.0f, 0.0f, 0.0f};
        const float r[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        const float s[3] = {half_x + soft, 1.0f, half_z + soft};
        const math::Mat4 model = math::multiply(placed, math::fromTrs(c, r, s));
        glUniformMatrix4fv(shadow_mvp_, 1, GL_FALSE, math::multiply(frame.view_projection, model).m);
        glUniform4f(shadow_shape_, half_x, half_z, soft, alpha);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    };

    if (!gameplay.driving()) {
        draw(warrior[0], warrior[2], 0.0f, 0.26f, 0.26f, 0.22f, strength);
    }
    for (const physics::Vehicle& car : gameplay.vehicles()) {
        const math::Vec3 p = car.renderPosition();
        const float c[3] = {p.x, p.y, p.z};
        if (!frame.frustum.intersectsSphere(c, 3.5f)) {
            continue;
        }
        // Fade as the car leaves the ground (jumps, flips).
        const float above = p.y - world_->groundHeight(p.x, p.z);
        const float fade = std::fmax(0.0f, std::fmin(1.0f, 1.0f - (above - 0.8f) / 2.0f));
        const math::Vec3 he = car.spec().half_extents;
        draw(p.x, p.z, car.yaw(), he.x * 0.95f, he.z * 0.95f, 0.45f, strength * 1.1f * fade);
    }
    glBindVertexArray(0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void CityPass::drawSky(const CityFrame& frame) {
    if (sky_program_ == 0) {
        return;
    }
    // Last, behind everything: only pixels still at the far plane are shaded.
    glDepthMask(GL_FALSE);
    glUseProgram(sky_program_);
    const float basis[16] = {
        frame.right[0], frame.right[1], frame.right[2], 0.0f,
        frame.up[0], frame.up[1], frame.up[2], 0.0f,
        frame.forward[0], frame.forward[1], frame.forward[2], 0.0f,
        frame.tan_half_fov * frame.aspect, frame.tan_half_fov, 0.0f, 0.0f,
    };
    glUniformMatrix4fv(sky_basis_, 1, GL_FALSE, basis);
    glUniform3fv(locations_->sky_zenith, 1, frame.atmosphere.sky_zenith);
    locations_->sky.set(frame.atmosphere, frame.eye);
    glBindVertexArray(sky_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
}

void CityPass::cleanup() {
    for (GLuint* vao : {&box_vao_, &floor_vao_, &sky_vao_, &shadow_vao_}) {
        if (*vao != 0) glDeleteVertexArrays(1, vao);
        *vao = 0;
    }
    for (GLuint* buffer : {&box_vbo_, &box_ibo_, &instance_vbo_, &floor_vbo_, &sky_vbo_, &shadow_vbo_}) {
        if (*buffer != 0) glDeleteBuffers(1, buffer);
        *buffer = 0;
    }
    for (GLuint* program : {&box_program_, &floor_program_, &sky_program_, &shadow_program_}) {
        if (*program != 0) glDeleteProgram(*program);
        *program = 0;
    }
    delete locations_;
    locations_ = nullptr;
}

} // namespace renderer
