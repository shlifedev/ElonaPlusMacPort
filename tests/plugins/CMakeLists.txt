# Standalone build of the plugin replacements and their tests, independent of
# the root host CMakeLists:
#   cmake -S tests/plugins -B build/plugins-macos -G Ninja && cmake --build build/plugins-macos
#   ctest --test-dir build/plugins-macos --output-on-failure
cmake_minimum_required(VERSION 3.24)
project(ElonaPluginsStandalone LANGUAGES C CXX)
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Plugin replacements target 64-bit native hosts only")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(ELONA_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include(CTest)
include("${ELONA_REPO_ROOT}/src/cmake/ElonaPlugins.cmake")
elona_add_plugins(TESTS ON)
