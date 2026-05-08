cmake_minimum_required(VERSION 3.20)

project(chi-tea-aye LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenGL REQUIRED)
find_package(X11 REQUIRED)

add_executable(chi-tea-aye
    src/main.cpp
    src/sokol_impl.cpp
    src/app/App.cpp
    src/renderer/Renderer.cpp
    src/gameplay/GameState.cpp
    src/world/World.cpp
    src/physics/Physics.cpp
    src/assets/AssetSystem.cpp
    src/debug/DebugUi.cpp
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
)

target_include_directories(chi-tea-aye PRIVATE
    src
    external/sokol
    external/imgui
    external/stb
    external/cgltf
    external/entt
    ${X11_INCLUDE_DIR}
)

target_compile_definitions(chi-tea-aye PRIVATE
    SOKOL_GLCORE
)

target_compile_options(chi-tea-aye PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

target_link_libraries(chi-tea-aye PRIVATE
    OpenGL::GL
    ${X11_LIBRARIES}
    ${X11_Xi_LIB}
    ${X11_Xcursor_LIB}
    dl
    pthread
    m
)
