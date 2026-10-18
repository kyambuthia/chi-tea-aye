#pragma once

#include "math/Frustum.h"
#include "math/Mat4.h"
#include "renderer/Atmosphere.h"
#include "renderer/GlApi.h"

#include <vector>

namespace gameplay {
class GameState;
}

namespace world {
class World;
}

namespace renderer {

// What every city draw needs from the frame.
struct CityFrame {
    math::Mat4 view_projection;
    math::Frustum frustum;
    float eye[3];
    float right[3];    // camera basis, for the sky's per-pixel view rays
    float up[3];
    float forward[3];
    float tan_half_fov;
    float aspect;
    Atmosphere atmosphere;
    float max_distance;  // instances farther than this are culled (they are lost in fog)
};

// The city: road surfaces, buildings with procedural facades, walls, street lamps and
// cars, all as boxes drawn in one instanced call after frustum and distance culling.
// Also the sky (drawn last, only where nothing else was) and blob contact shadows.
class CityPass {
public:
    // Requires a current GL context. The world must outlive the pass.
    bool init(const world::World& world);
    void drawOpaque(const CityFrame& frame, const gameplay::GameState& gameplay);
    // Soft contact shadows under the warrior (unless driving) and every car.
    void drawShadows(const CityFrame& frame, const gameplay::GameState& gameplay, const float warrior[3]);
    void drawSky(const CityFrame& frame);
    void cleanup();

    int visibleInstances() const { return visible_instances_; }
    int totalInstances() const { return static_cast<int>(static_instances_.size()) + car_instances_; }

    // One box: model matrix (unit cube -> world), colour (rgb + kind), and parameters.
    struct Instance {
        float model[16];
        float color[4];
        float params[4];
    };

private:
    struct StaticInstance {
        Instance instance;
        float lo[3];
        float hi[3];
    };
    void appendCar(const gameplay::GameState& gameplay, int index, std::vector<Instance>& out) const;

    const world::World* world_ = nullptr;
    std::vector<StaticInstance> static_instances_;
    std::vector<Instance> frame_instances_;
    int visible_instances_ = 0;
    int car_instances_ = 0;

    GLuint box_program_ = 0;
    GLint box_vp_ = -1;
    GLuint box_vao_ = 0;
    GLuint box_vbo_ = 0;
    GLuint box_ibo_ = 0;
    GLuint instance_vbo_ = 0;
    size_t instance_capacity_ = 0;

    GLuint floor_program_ = 0;
    GLint floor_vp_ = -1;
    GLuint floor_vao_ = 0;
    GLuint floor_vbo_ = 0;
    GLsizei floor_vertex_count_ = 0;

    GLuint sky_program_ = 0;
    GLint sky_basis_ = -1;
    GLuint sky_vao_ = 0;
    GLuint sky_vbo_ = 0;

    GLuint shadow_program_ = 0;
    GLint shadow_mvp_ = -1;
    GLint shadow_shape_ = -1;
    GLuint shadow_vao_ = 0;
    GLuint shadow_vbo_ = 0;

    // Atmosphere uniform locations per program (box, floor, sky).
    struct Locations;
    Locations* locations_ = nullptr;
};

} // namespace renderer
