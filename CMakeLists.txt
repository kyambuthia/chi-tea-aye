cmake_minimum_required(VERSION 3.20)

project(chi-tea-aye LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# GLES 3 on web and Android; desktop uses GL 3.3 core.
if(EMSCRIPTEN OR ANDROID)
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
    # Android loads SDL3 as a shared library through SDLActivity; desktop links it statically.
    if(ANDROID)
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    else()
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
    endif()
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

# Character and world logic, shared by the game and the tests.
add_library(chi-character STATIC
    src/assets/CharacterModel.cpp
    src/world/World.cpp
    src/gameplay/WeaponState.cpp
    src/assets/StaticModel.cpp
)
target_include_directories(chi-character PUBLIC src external/cgltf external/stb)
target_link_libraries(chi-character PUBLIC ozz_animation_offline ozz_animation ozz_base)

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
endif()

set(CHI_SOURCES
    src/main.cpp
    src/app/App.cpp
    src/renderer/Renderer.cpp
    src/renderer/GlFunctions.cpp
    src/gameplay/GameState.cpp
    src/physics/Physics.cpp
    src/assets/AssetSystem.cpp
    src/debug/DebugUi.cpp
)

# SDLActivity loads a shared library named "main", so Android builds a library
# under that name; every other platform builds an executable.
if(ANDROID)
    set(CHI_APP_TARGET main)
    add_library(${CHI_APP_TARGET} SHARED ${CHI_SOURCES})
else()
    set(CHI_APP_TARGET chi-tea-aye)
    add_executable(${CHI_APP_TARGET} ${CHI_SOURCES})
endif()

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

target_link_libraries(${CHI_APP_TARGET} PRIVATE chi-character imgui SDL3::SDL3)

if(EMSCRIPTEN)
    # Emscripten emits an HTML shell alongside the JS and WASM.
    set_target_properties(${CHI_APP_TARGET} PROPERTIES SUFFIX ".html")
    target_link_options(${CHI_APP_TARGET} PRIVATE --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
    # Bundle the assets into the virtual filesystem at the same relative paths the game loads.
    target_link_options(${CHI_APP_TARGET} PRIVATE --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets)
endif()

if(ANDROID)
    target_link_libraries(${CHI_APP_TARGET} PRIVATE GLESv3 log)
elseif(NOT CHI_GLES)
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
if(CHI_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(clip_check tests/clip_check.cpp)
    target_link_libraries(clip_check PRIVATE chi-character)
    add_executable(foot_contact_check tests/foot_contact_check.cpp)
    target_link_libraries(foot_contact_check PRIVATE chi-character)
    add_executable(weapon_attachment_check tests/weapon_attachment_check.cpp)
    target_include_directories(weapon_attachment_check PRIVATE src)
    add_executable(weapon_state_check tests/weapon_state_check.cpp src/gameplay/WeaponState.cpp)
    target_include_directories(weapon_state_check PRIVATE src)
    add_executable(hand_ik_check tests/hand_ik_check.cpp)
    target_link_libraries(hand_ik_check PRIVATE chi-character)
    add_executable(knee_check tests/knee_check.cpp)
    target_link_libraries(knee_check PRIVATE chi-character)
    add_executable(clip_direction_check tests/clip_direction_check.cpp)
    target_link_libraries(clip_direction_check PRIVATE chi-character)
    add_executable(movement_check tests/movement_check.cpp src/gameplay/GameState.cpp src/gameplay/WeaponState.cpp src/physics/Physics.cpp)
    target_include_directories(movement_check PRIVATE src)
endif()
