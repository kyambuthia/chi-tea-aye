cmake_minimum_required(VERSION 3.20)

project(chi-tea-aye LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Jolt Physics provides the collision and rigid-body runtime used by chi-sim.
# Keep its optional compute backends and standalone install rules out of this build.
include(FetchContent)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
# Use Jolt's SSE2 baseline on x86 so the desktop build runs on older CPUs too.
foreach(feature SSE4_1 SSE4_2 AVX AVX2 AVX512 LZCNT TZCNT F16C FMADD)
    set(USE_${feature} OFF CACHE BOOL "" FORCE)
endforeach()
FetchContent_Declare(JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.6.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR Build
)
FetchContent_MakeAvailable(JoltPhysics)

# GLES 3 on web; desktop uses GL 3.3 core.
if(EMSCRIPTEN)
    set(CHI_GLES ON)
else()
    set(CHI_GLES OFF)
endif()

if(EMSCRIPTEN)
    # Emscripten ships SDL3 as a port; no submodule build needed.
    add_library(chi-sdl3 INTERFACE)
    add_library(SDL3::SDL3 ALIAS chi-sdl3)
    target_compile_options(chi-sdl3 INTERFACE -sUSE_SDL=3)
    target_link_options(chi-sdl3 INTERFACE -sUSE_SDL=3 -sUSE_WEBGL2=1 -sALLOW_MEMORY_GROWTH=1)
else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/SDL)
endif()

# ozz-animation: the runtime and offline builders only; no tools, samples, or importers.
    set(ozz_build_tools OFF CACHE BOOL "" FORCE)
    set(ozz_build_fbx OFF CACHE BOOL "" FORCE)
    set(ozz_build_gltf OFF CACHE BOOL "" FORCE)
    set(ozz_build_samples OFF CACHE BOOL "" FORCE)
    set(ozz_build_howtos OFF CACHE BOOL "" FORCE)
    set(ozz_build_tests OFF CACHE BOOL "" FORCE)
    set(ozz_build_data OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/ozz-animation)

# The world: terrain, the city layout, and its spatial grid. No GL, no SDL.
add_library(chi-world STATIC
    src/world/World.cpp
    src/world/City.cpp
    src/world/SpatialGrid.cpp
)
target_include_directories(chi-world PUBLIC src)

# Character models and props, shared by the game and the tests.
add_library(chi-character STATIC
    src/assets/CharacterModel.cpp
    src/assets/StaticModel.cpp
)
target_include_directories(chi-character PUBLIC src external/cgltf external/stb)
target_link_libraries(chi-character PUBLIC chi-world ozz_animation_offline ozz_animation ozz_base)

# Simulation: gameplay state, weapons, targets, movement, and vehicles. Testable headless.
add_library(chi-sim STATIC
    src/gameplay/GameState.cpp
    src/gameplay/Targets.cpp
    src/gameplay/WeaponState.cpp
    src/physics/Physics.cpp
    src/physics/Vehicle.cpp
)
target_include_directories(chi-sim PUBLIC src)
target_link_libraries(chi-sim PUBLIC chi-world chi-character Jolt)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui")
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)
if(CHI_GLES)
    target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3)
    # Browsers stall on every GL state query; the backend's per-frame save and restore
    # of GL state is answered without asking GL (see the header for why that is safe).
    set_source_files_properties(${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp PROPERTIES
        COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/src/debug/ImGuiGlShadow.h")
endif()

set(CHI_SOURCES
    src/main.cpp
    src/app/App.cpp
    src/renderer/Renderer.cpp
    src/renderer/GlFunctions.cpp
    src/ui/WeaponWheel.cpp
    src/ui/Hud.cpp
    src/assets/AssetSystem.cpp
    src/renderer/GlUtil.cpp
    src/renderer/CityPass.cpp
    src/renderer/PostPass.cpp
    src/debug/DebugUi.cpp
)

set(CHI_APP_TARGET chi-tea-aye)
add_executable(${CHI_APP_TARGET} ${CHI_SOURCES})

target_include_directories(${CHI_APP_TARGET} PRIVATE
    src
    external/stb
    external/cgltf
    external/entt
)

if(CHI_GLES)
    target_compile_definitions(${CHI_APP_TARGET} PRIVATE CHI_GLES)
endif()

target_compile_options(${CHI_APP_TARGET} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

target_link_libraries(${CHI_APP_TARGET} PRIVATE chi-sim chi-character imgui SDL3::SDL3)

if(EMSCRIPTEN)
    # Emscripten emits an HTML shell alongside the JS and WASM.
    set_target_properties(${CHI_APP_TARGET} PROPERTIES OUTPUT_NAME "index" SUFFIX ".html")
    # Match Jolt 5.6's Emscripten examples: physics needs more than the 64 KiB default.
    # Keep a stack cookie so a future overflow fails clearly instead of corrupting input state.
    target_link_options(${CHI_APP_TARGET} PRIVATE -sSTACK_SIZE=1048576 -sSTACK_OVERFLOW_CHECK=1)
    target_link_options(${CHI_APP_TARGET} PRIVATE --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
    # Relink when the page shell changes, not only when the code does.
    set_property(TARGET ${CHI_APP_TARGET} APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
    # Bundle the assets into the virtual filesystem at the same relative paths the game loads.
    target_link_options(${CHI_APP_TARGET} PRIVATE --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets)

    # Copy PWA assets to the build output directory
    add_custom_command(TARGET ${CHI_APP_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/web/manifest.webmanifest
            $<TARGET_FILE_DIR:${CHI_APP_TARGET}>/manifest.webmanifest
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/web/sw.js
            $<TARGET_FILE_DIR:${CHI_APP_TARGET}>/sw.js
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/web/icon-192.png
            $<TARGET_FILE_DIR:${CHI_APP_TARGET}>/icon-192.png
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/web/icon-512.png
            $<TARGET_FILE_DIR:${CHI_APP_TARGET}>/icon-512.png
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE_DIR:${CHI_APP_TARGET}>/index.html
            $<TARGET_FILE_DIR:${CHI_APP_TARGET}>/chi-tea-aye.html
        COMMENT "Copying PWA web assets to output directory"
    )
endif()

if(NOT CHI_GLES)
    # Desktop: GL 1.0 entry points come from the system GL library; newer ones are
    # loaded at runtime (see src/renderer/GlFunctions.cpp).
    find_package(OpenGL REQUIRED)
    target_link_libraries(${CHI_APP_TARGET} PRIVATE OpenGL::GL)
endif()

# Locomotion preprocessing: foot contacts and strides from the clips.
add_library(chi-locomotion STATIC locomotion/Contacts.cpp)
target_include_directories(chi-locomotion PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(locomotion_preprocess locomotion/preprocess.cpp)
target_link_libraries(locomotion_preprocess PRIVATE chi-character chi-locomotion)

# Unit-style checks, built and run from the repository root.
option(CHI_BUILD_TESTS "Build the character and terrain checks" ON)
if(CHI_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(clip_check tests/clip_check.cpp)
    target_link_libraries(clip_check PRIVATE chi-character)
    add_executable(foot_contact_check tests/foot_contact_check.cpp)
    target_link_libraries(foot_contact_check PRIVATE chi-character)
    add_executable(weapon_attachment_check tests/weapon_attachment_check.cpp)
    target_include_directories(weapon_attachment_check PRIVATE src)
    add_executable(weapon_state_check tests/weapon_state_check.cpp)
    target_link_libraries(weapon_state_check PRIVATE chi-sim)
    add_executable(hand_ik_check tests/hand_ik_check.cpp)
    target_link_libraries(hand_ik_check PRIVATE chi-character)
    add_executable(knee_check tests/knee_check.cpp)
    target_link_libraries(knee_check PRIVATE chi-character)
    add_executable(clip_direction_check tests/clip_direction_check.cpp)
    target_link_libraries(clip_direction_check PRIVATE chi-character)
    add_executable(movement_check tests/movement_check.cpp)
    target_link_libraries(movement_check PRIVATE chi-sim)
    add_executable(ragdoll_check tests/ragdoll_check.cpp)
    target_link_libraries(ragdoll_check PRIVATE chi-sim)
    add_executable(target_check tests/target_check.cpp)
    target_link_libraries(target_check PRIVATE chi-sim)
    add_executable(wheel_check tests/wheel_check.cpp src/ui/WeaponWheel.cpp)
    target_include_directories(wheel_check PRIVATE src)
    add_executable(world_check tests/world_check.cpp)
    target_link_libraries(world_check PRIVATE chi-world)
    add_executable(vehicle_check tests/vehicle_check.cpp)
    target_link_libraries(vehicle_check PRIVATE chi-sim)
    add_executable(render_math_check tests/render_math_check.cpp)
    target_include_directories(render_math_check PRIVATE src)
    add_executable(hud_check tests/hud_check.cpp)
    target_include_directories(hud_check PRIVATE src)
endif()
