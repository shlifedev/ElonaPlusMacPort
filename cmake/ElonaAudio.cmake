# Native music renderer (MIDI via SoundFont, MP3) for ElonaPlus 2.335.
#
# Integration (root CMakeLists.txt, owned by the GUI/host worker):
#   include("${CMAKE_SOURCE_DIR}/cmake/ElonaAudio.cmake")
#   elona_add_audio(TESTS ${BUILD_TESTING})
#   target_link_libraries(<host target> PRIVATE elona_audio)
#   #include "audio/music.h"
#   SoundFont: ${ELONA_AUDIO_SOUNDFONT} (target property / cache variable)
#
# Inputs come from `python3 tools/bootstrap_audio.py` (pinned commits and
# SHA-256, private .cache/audio). No system packages and no runtime DLLs.
include_guard(GLOBAL)
get_filename_component(ELONA_AUDIO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(ELONA_AUDIO_CACHE "${ELONA_AUDIO_ROOT}/.cache/audio" CACHE PATH "Pinned audio inputs (tools/bootstrap_audio.py)")
set(ELONA_AUDIO_SOUNDFONT "${ELONA_AUDIO_CACHE}/GeneralUser-GS/GeneralUser-GS.sf2" CACHE FILEPATH
    "SoundFont used for MIDI (GeneralUser GS v2.0.3)")

function(_elona_audio_check_inputs)
  foreach(rel TinySoundFont/tsf.h dr_libs/dr_mp3.h)
    if(NOT EXISTS "${ELONA_AUDIO_CACHE}/${rel}")
      message(FATAL_ERROR "Missing ${ELONA_AUDIO_CACHE}/${rel}; run: python3 tools/bootstrap_audio.py")
    endif()
  endforeach()
  set(expected_tsf "70d55963c98f60ebb81518eaa1f25d46888d5180eb5f5289fd6b74ffc177d197")
  set(expected_mp3 "f5c8020eeebb79877184432efdd2651f24869f5bb8f379d45450fd0aed1ee915")
  file(SHA256 "${ELONA_AUDIO_CACHE}/TinySoundFont/tsf.h" got_tsf)
  file(SHA256 "${ELONA_AUDIO_CACHE}/dr_libs/dr_mp3.h" got_mp3)
  if(NOT got_tsf STREQUAL expected_tsf OR NOT got_mp3 STREQUAL expected_mp3)
    message(FATAL_ERROR "Audio third-party headers do not match the pinned SHA-256; rerun tools/bootstrap_audio.py")
  endif()
endfunction()

# elona_add_audio([TESTS <bool>])
function(elona_add_audio)
  cmake_parse_arguments(ARG "" "TESTS" "" ${ARGN})
  if(TARGET elona_audio)
    return()
  endif()
  _elona_audio_check_inputs()
  set(dir "${ELONA_AUDIO_ROOT}/src/audio")
  add_library(elona_audio STATIC
    "${dir}/smf.cpp"
    "${dir}/midi_stream.cpp"
    "${dir}/mp3_stream.cpp"
    "${dir}/music.cpp"
    "${dir}/third_party_impl.cpp")
  target_include_directories(elona_audio PUBLIC "${ELONA_AUDIO_ROOT}/src")
  target_include_directories(elona_audio SYSTEM PRIVATE "${ELONA_AUDIO_CACHE}/TinySoundFont" "${ELONA_AUDIO_CACHE}/dr_libs")
  target_compile_features(elona_audio PUBLIC cxx_std_17)
  set_target_properties(elona_audio PROPERTIES ELONA_AUDIO_SOUNDFONT "${ELONA_AUDIO_SOUNDFONT}")
  if(MSVC)
    target_compile_options(elona_audio PRIVATE /utf-8)
    target_compile_definitions(elona_audio PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    set_source_files_properties("${dir}/third_party_impl.cpp" PROPERTIES COMPILE_OPTIONS "/W0")
    set_source_files_properties("${dir}/smf.cpp" "${dir}/midi_stream.cpp" "${dir}/mp3_stream.cpp" "${dir}/music.cpp"
      PROPERTIES COMPILE_OPTIONS "/W4")
  else()
    set_source_files_properties("${dir}/third_party_impl.cpp" PROPERTIES COMPILE_OPTIONS "-w")
    set_source_files_properties("${dir}/smf.cpp" "${dir}/midi_stream.cpp" "${dir}/mp3_stream.cpp" "${dir}/music.cpp"
      PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
  endif()
  if(UNIX AND NOT APPLE)
    target_link_libraries(elona_audio PRIVATE m)
  endif()

  # Offline renderer (MIDI/MP3 -> WAV) for listening checks and Windows offline validation.
  add_executable(elona_music_render "${ELONA_AUDIO_ROOT}/tests/audio/render_tool.cpp")
  target_link_libraries(elona_music_render PRIVATE elona_audio)
  target_compile_definitions(elona_music_render PRIVATE ELONA_AUDIO_DEFAULT_SOUNDFONT="${ELONA_AUDIO_SOUNDFONT}")

  if(ARG_TESTS)
    add_executable(elona_audio_tests
      "${ELONA_AUDIO_ROOT}/tests/audio/audio_tests.cpp"
      "${ELONA_AUDIO_ROOT}/tests/audio/game_render_tests.cpp")
    target_link_libraries(elona_audio_tests PRIVATE elona_audio)
    target_include_directories(elona_audio_tests PRIVATE "${ELONA_AUDIO_ROOT}/tests/audio")
    if(MSVC)
      target_compile_options(elona_audio_tests PRIVATE /utf-8)
      target_compile_definitions(elona_audio_tests PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    endif()
    target_compile_definitions(elona_audio_tests PRIVATE ELONA_AUDIO_DEFAULT_SOUNDFONT="${ELONA_AUDIO_SOUNDFONT}")
    if(COMMAND add_test)
      add_test(NAME audio_unit COMMAND elona_audio_tests)
    endif()
  endif()
endfunction()
