cmake_minimum_required(VERSION 3.20)

# Gradle's CMake entry point. The game is built from the repository root so the
# desktop, web, and Android targets share one set of sources and one CMakeLists.
project(chi-tea-aye-android LANGUAGES C CXX)

get_filename_component(CHI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_subdirectory("${CHI_ROOT}" chi-tea-aye)
