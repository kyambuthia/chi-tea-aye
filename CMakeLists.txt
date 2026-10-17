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
    src/world/World.cpp
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

target_link_libraries(${CHI_APP_TARGET} PRIVATE imgui SDL3::SDL3)

if(EMSCRIPTEN)
    # Emscripten emits an HTML shell alongside the JS and WASM.
    set_target_properties(${CHI_APP_TARGET} PROPERTIES SUFFIX ".html")
    target_link_options(${CHI_APP_TARGET} PRIVATE --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
endif()

if(ANDROID)
    target_link_libraries(${CHI_APP_TARGET} PRIVATE GLESv3 log)
elseif(NOT CHI_GLES)
    # Desktop: GL 1.0 entry points come from the system GL library; newer ones are
    # loaded at runtime (see src/renderer/GlFunctions.cpp).
    find_package(OpenGL REQUIRED)
    target_link_libraries(${CHI_APP_TARGET} PRIVATE OpenGL::GL)
endif()
