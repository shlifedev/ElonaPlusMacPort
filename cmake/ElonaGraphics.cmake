# CPU pixel model, optional SDL3 window backend, and the native GUI host.
#
#   ELONA_GUI       AUTO (default) / ON / OFF  - SDL3 window + event loop
#   ELONA_TEXT_TTF  AUTO (default) / ON / OFF  - SDL3_ttf glyph source for mes/print
#
# With both OFF only the headless pixel model and host are built (no external
# dependencies), which keeps headless-only Windows builds possible.

set(ELONA_GUI "AUTO" CACHE STRING "Build the SDL3 window backend (AUTO/ON/OFF)")
set(ELONA_TEXT_TTF "AUTO" CACHE STRING "Use SDL3_ttf for text rendering (AUTO/ON/OFF)")
set_property(CACHE ELONA_GUI PROPERTY STRINGS AUTO ON OFF)
set_property(CACHE ELONA_TEXT_TTF PROPERTY STRINGS AUTO ON OFF)

set(_elona_need_sdl OFF)
if(ELONA_MACOS_PINNED_GUI)
  foreach(_sdl_pkg IN ITEMS SDL3 SDL3_ttf)
    if(NOT EXISTS "${${_sdl_pkg}_DIR}/${_sdl_pkg}Config.cmake")
      message(FATAL_ERROR "Missing pinned ${_sdl_pkg} framework. Run python3 tools/bootstrap_macos_gui.py before configuring the macOS Release preset.")
    endif()
  endforeach()
endif()
if(NOT ELONA_GUI STREQUAL "OFF" OR NOT ELONA_TEXT_TTF STREQUAL "OFF")
  find_package(SDL3 CONFIG QUIET)
endif()
if(NOT ELONA_TEXT_TTF STREQUAL "OFF")
  find_package(SDL3_ttf CONFIG QUIET)
  if(ELONA_MACOS_PINNED_GUI AND TARGET SDL3_ttf::SDL3_ttf-shared)
    # The official 3.2.2 macOS package uses a misspelled variable for its
    # imported location, which otherwise resolves to /SDL3_ttf.
    get_filename_component(_ttf_framework "${SDL3_ttf_DIR}/../.." ABSOLUTE)
    set_property(TARGET SDL3_ttf::SDL3_ttf-shared PROPERTY IMPORTED_LOCATION "${_ttf_framework}/SDL3_ttf")
  endif()
endif()

set(ELONA_GUI_ENABLED OFF)
if(ELONA_GUI STREQUAL "ON" AND NOT TARGET SDL3::SDL3)
  message(FATAL_ERROR "ELONA_GUI=ON but SDL3 was not found (set SDL3_DIR or CMAKE_PREFIX_PATH)")
endif()
if(NOT ELONA_GUI STREQUAL "OFF" AND TARGET SDL3::SDL3)
  set(ELONA_GUI_ENABLED ON)
endif()
set(ELONA_TTF_ENABLED OFF)
if(ELONA_TEXT_TTF STREQUAL "ON" AND NOT TARGET SDL3_ttf::SDL3_ttf)
  message(FATAL_ERROR "ELONA_TEXT_TTF=ON but SDL3_ttf was not found")
endif()
if(NOT ELONA_TEXT_TTF STREQUAL "OFF" AND TARGET SDL3_ttf::SDL3_ttf AND TARGET SDL3::SDL3)
  set(ELONA_TTF_ENABLED ON)
endif()
message(STATUS "ElonaPlus GUI window backend: ${ELONA_GUI_ENABLED}; SDL3_ttf text: ${ELONA_TTF_ENABLED}")

add_library(elona_graphics STATIC
  src/graphics/hsp_screen.cpp
  src/graphics/image_io.cpp
  src/graphics/poly_raster.cpp
  src/graphics/ui_text_codec.cpp
  src/localization/catalog.cpp
  src/localization/adventure_log.cpp
  src/localization/quest_board.cpp
  src/localization/item_label.cpp
  src/localization/rich_text.cpp)
target_include_directories(elona_graphics PUBLIC "${CMAKE_SOURCE_DIR}/src")
# stb_image.h (public domain) vendored by the pinned OpenHSP checkout
target_include_directories(elona_graphics PRIVATE "${HSP3}/win32gui")
if(ELONA_TTF_ENABLED)
  target_sources(elona_graphics PRIVATE src/graphics/text_renderer_ttf.cpp)
  target_link_libraries(elona_graphics PUBLIC SDL3_ttf::SDL3_ttf SDL3::SDL3)
  target_compile_definitions(elona_graphics PUBLIC ELONA_HAVE_TTF)
endif()
if(TARGET elona_platform)
  target_link_libraries(elona_graphics PUBLIC elona_platform)
  target_compile_definitions(elona_graphics PRIVATE ELONA_HAVE_PLATFORM_ENCODING)
elseif(APPLE)
  target_link_libraries(elona_graphics PUBLIC iconv)  # temporary UI codec backend
endif()

add_executable(elona_gui_host
  src/app/main.cpp
  src/app/gui_host.cpp
  src/app/native_dll.cpp
  src/app/dll_bindings_plugins.cpp
  src/app/dll_bindings_win32.cpp
  src/app/dll_bindings_media.cpp
  src/app/dll_bindings_gfx.cpp
  src/platform/window_headless.cpp
  src/platform/audio_null.cpp
  src/platform/keymap_vk.cpp)
target_link_libraries(elona_gui_host PRIVATE elona_hsp_core elona_graphics)
target_include_directories(elona_gui_host PRIVATE "${CMAKE_SOURCE_DIR}/src")
if(TARGET elona_platform_hsp)
  target_link_libraries(elona_gui_host PRIVATE elona_platform_hsp)
  target_compile_definitions(elona_gui_host PRIVATE ELONA_HAVE_GAME_FILES)
endif()
if(TARGET elona_audio)
  target_link_libraries(elona_gui_host PRIVATE elona_audio)
  get_target_property(_elona_sf elona_audio ELONA_AUDIO_SOUNDFONT)
  target_compile_definitions(elona_gui_host PRIVATE ELONA_HAVE_MUSIC ELONA_DEFAULT_SOUNDFONT="${_elona_sf}")
endif()
if(TARGET elona_plugins)
  target_link_libraries(elona_gui_host PRIVATE elona_plugins)
  target_compile_definitions(elona_gui_host PRIVATE ELONA_HAVE_PLUGINS)
endif()
if(ELONA_GUI_ENABLED)
  target_sources(elona_gui_host PRIVATE src/platform/window_sdl.cpp src/platform/audio_sdl.cpp)
  target_link_libraries(elona_gui_host PRIVATE SDL3::SDL3)
  target_compile_definitions(elona_gui_host PRIVATE ELONA_GUI)
endif()

foreach(target elona_graphics elona_gui_host)
  if(MSVC)
    target_compile_options(${target} PRIVATE /utf-8)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wno-write-strings)
  endif()
endforeach()
if(ELONA_LEGACY_CP932)
  target_compile_definitions(elona_gui_host PRIVATE ELONA_LEGACY_CP932)
endif()
if(ELONA_LEGACY_RNG)
  target_compile_definitions(elona_gui_host PRIVATE ELONA_LEGACY_RNG)
endif()

if(BUILD_TESTING)
  add_executable(elona_catalog_tests tests/localization/catalog_tests.cpp src/localization/catalog.cpp src/localization/adventure_log.cpp src/localization/quest_board.cpp src/localization/item_label.cpp src/localization/rich_text.cpp)
  target_link_libraries(elona_catalog_tests PRIVATE elona_platform)
  target_include_directories(elona_catalog_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
  if(MSVC)
    target_compile_options(elona_catalog_tests PRIVATE /utf-8)
  endif()
  add_test(NAME localization_catalog COMMAND elona_catalog_tests "${CMAKE_BINARY_DIR}/catalog-test")
  # Hidden SDL windows still require a desktop session, so the native event
  # boundary probe is opt-in. The HSP composition/CP932 assertions run headless.
  option(ELONA_TEST_DESKTOP "Run tests that require an interactive desktop" OFF)
  if(ELONA_GUI_ENABLED)
    add_executable(elona_sdl_ime_tests tests/graphics/sdl_ime_events.cpp
      src/platform/window_sdl.cpp src/platform/keymap_vk.cpp)
    target_link_libraries(elona_sdl_ime_tests PRIVATE elona_graphics SDL3::SDL3)
    target_compile_definitions(elona_sdl_ime_tests PRIVATE ELONA_GUI)
    if(MSVC)
      target_compile_options(elona_sdl_ime_tests PRIVATE /utf-8)
    endif()
    if(ELONA_TEST_DESKTOP)
      add_test(NAME sdl_ime_events COMMAND elona_sdl_ime_tests)
    endif()
  endif()
  add_executable(elona_graphics_tests tests/graphics/surface_tests.cpp)
  target_link_libraries(elona_graphics_tests PRIVATE elona_graphics)
  if(MSVC)
    target_compile_options(elona_graphics_tests PRIVATE /utf-8)
  endif()
  add_test(NAME graphics_surface COMMAND elona_graphics_tests)
  if(Python3_FOUND)
    add_test(NAME localization_compile COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/localization/test_compile.py")
    if(ELONA_TTF_ENABLED)
      add_test(NAME localization_display COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/localization/run_display.py"
        --compiler "$<TARGET_FILE:elona_hspcmp>" --host "$<TARGET_FILE:elona_gui_host>" --common "${OPENHSP_ROOT}/common"
        --work "${CMAKE_BINARY_DIR}/localized-display")
    endif()
    add_test(NAME gui_exit_diagnostics COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/graphics/run_exit_diagnostics.py"
      --compiler "$<TARGET_FILE:elona_hspcmp>" --host "$<TARGET_FILE:elona_gui_host>" --common "${OPENHSP_ROOT}/common"
      --work "${CMAKE_BINARY_DIR}/exit-diagnostics" --unique)
    add_test(NAME gui_ime_headless COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/graphics/run_ime_fixture.py"
      --compiler "$<TARGET_FILE:elona_hspcmp>" --host "$<TARGET_FILE:elona_gui_host>" --common "${OPENHSP_ROOT}/common"
      --work "${CMAKE_BINARY_DIR}/ime-headless" --unique)
    set(_gui_flags)
    if(NOT ELONA_TTF_ENABLED)
      list(APPEND _gui_flags --no-text)
    endif()
    add_test(NAME gui_fixtures_headless COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/graphics/run_gui_fixtures.py"
      --compiler "$<TARGET_FILE:elona_hspcmp>" --host "$<TARGET_FILE:elona_gui_host>" --common "${OPENHSP_ROOT}/common"
      --work "${CMAKE_BINARY_DIR}/gui-fixtures-headless" --headless ${_gui_flags})
  endif()
endif()
