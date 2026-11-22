# Standalone build of the music renderer and its tests:
#   python3 scripts/bootstrap_audio.py
#   cmake -S tests/audio -B build/audio-macos -G Ninja && cmake --build build/audio-macos
#   ctest --test-dir build/audio-macos --output-on-failure
#   build/audio-macos/elona_audio_tests --game-dir elonaplus2.335 --report work/audio/render-report.json
cmake_minimum_required(VERSION 3.24)
project(ElonaAudioStandalone LANGUAGES C CXX)
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "The music renderer targets 64-bit native hosts only")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(ELONA_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include(CTest)
include("${ELONA_REPO_ROOT}/src/cmake/ElonaAudio.cmake")
elona_add_audio(TESTS ON)

# Explicit opt-in: this probe opens the real default playback device and is
# deliberately not part of CTest/CI (which may have no audio hardware).
option(ELONA_AUDIO_DEVICE_PROBE "Build the SDL device integration probe" OFF)
if(ELONA_AUDIO_DEVICE_PROBE)
  find_package(SDL3 CONFIG REQUIRED)
  add_executable(elona_audio_device_probe device_probe.cpp
    "${ELONA_REPO_ROOT}/src/platform/audio_sdl.cpp"
    "${ELONA_REPO_ROOT}/src/platform/audio_null.cpp")
  target_link_libraries(elona_audio_device_probe PRIVATE elona_audio SDL3::SDL3)
  target_compile_definitions(elona_audio_device_probe PRIVATE ELONA_GUI
    ELONA_AUDIO_DEFAULT_SOUNDFONT="${ELONA_AUDIO_SOUNDFONT}")
endif()
