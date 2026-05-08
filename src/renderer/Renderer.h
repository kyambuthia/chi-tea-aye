#pragma once

namespace debug {
class DebugUi;
}

namespace renderer {

class Renderer {
public:
    void init();
    void draw(debug::DebugUi& debug_ui);
    void cleanup();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace renderer
