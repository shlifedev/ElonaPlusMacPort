# Portable replacements for the ElonaPlus 2.335 plugin contracts.
#
# Integration (root CMakeLists.txt, owned by the graphics/host worker):
#   include("${CMAKE_SOURCE_DIR}/src/cmake/ElonaPlugins.cmake")
#   elona_add_plugins(TESTS ${BUILD_TESTING})
#   target_link_libraries(<host target> PRIVATE elona_plugins)
#   #include "plugins/hmm_bits.h" / "plugins/exrand.h" / "plugins/zgzip.h" / "plugins/hspda.h"
#
# Targets:
#   elona_plugins        STATIC, C++17, PUBLIC include dir = <repo>/src
#   elona_zlib           STATIC pinned zlib 1.3.1 core (no gz* files), unless
#                        ELONA_PLUGINS_SYSTEM_ZLIB=ON (then ZLIB::ZLIB)
#   elona_plugins_tests  test executable + CTest entry `plugins_unit` (TESTS ON)
#
# No original DLL is loaded or linked. The zlib archive is verified by SHA-256.
include_guard(GLOBAL)
get_filename_component(ELONA_PLUGINS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(ELONA_ZLIB_VERSION "1.3.1")
set(ELONA_ZLIB_SHA256 "9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23")
set(ELONA_ZLIB_URL "https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz")
set(ELONA_ZLIB_ARCHIVE "${ELONA_PLUGINS_ROOT}/.cache/downloads/zlib-1.3.1.tar.gz" CACHE FILEPATH
    "Pinned zlib ${ELONA_ZLIB_VERSION} source archive (downloaded and hash-checked if missing)")
option(ELONA_PLUGINS_SYSTEM_ZLIB "Link the system zlib instead of the pinned zlib ${ELONA_ZLIB_VERSION}" OFF)

function(_elona_plugins_pinned_zlib)
  if(TARGET elona_zlib)
    return()
  endif()
  if(NOT EXISTS "${ELONA_ZLIB_ARCHIVE}")
    message(STATUS "Downloading zlib ${ELONA_ZLIB_VERSION}")
    file(DOWNLOAD "${ELONA_ZLIB_URL}" "${ELONA_ZLIB_ARCHIVE}" EXPECTED_HASH SHA256=${ELONA_ZLIB_SHA256} TLS_VERIFY ON)
  endif()
  file(SHA256 "${ELONA_ZLIB_ARCHIVE}" actual)
  if(NOT actual STREQUAL ELONA_ZLIB_SHA256)
    message(FATAL_ERROR "zlib archive hash mismatch: ${ELONA_ZLIB_ARCHIVE} (${actual})")
  endif()
  set(root "${CMAKE_BINARY_DIR}/_deps/zlib-${ELONA_ZLIB_VERSION}")
  set(stamp "${root}/.elona-sha256")
  set(stamp_value "")
  if(EXISTS "${stamp}")
    file(READ "${stamp}" stamp_value)
  endif()
  if(NOT stamp_value STREQUAL ELONA_ZLIB_SHA256)
    file(REMOVE_RECURSE "${root}")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_deps")
    file(ARCHIVE_EXTRACT INPUT "${ELONA_ZLIB_ARCHIVE}" DESTINATION "${CMAKE_BINARY_DIR}/_deps")
    file(WRITE "${stamp}" "${ELONA_ZLIB_SHA256}")
  endif()
  set(sources)
  foreach(name adler32 compress crc32 deflate infback inffast inflate inftrees trees uncompr zutil)
    list(APPEND sources "${root}/${name}.c")
  endforeach()
  add_library(elona_zlib STATIC ${sources})
  target_include_directories(elona_zlib PUBLIC "${root}")
  set_target_properties(elona_zlib PROPERTIES C_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON)
  if(MSVC)
    target_compile_definitions(elona_zlib PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
  else()
    # zlib's own sources need unistd-style off_t; quiet upstream-only warnings.
    target_compile_definitions(elona_zlib PRIVATE HAVE_UNISTD_H)
  endif()
endfunction()

# elona_add_plugins([TESTS <bool>])
function(elona_add_plugins)
  cmake_parse_arguments(ARG "" "TESTS" "" ${ARGN})
  if(TARGET elona_plugins)
    return()
  endif()
  set(dir "${ELONA_PLUGINS_ROOT}/src/plugins")
  add_library(elona_plugins STATIC
    "${dir}/plugin_result.cpp"
    "${dir}/exrand.cpp"
    "${dir}/zgzip.cpp"
    "${dir}/hspda.cpp")
  target_include_directories(elona_plugins PUBLIC "${ELONA_PLUGINS_ROOT}/src")
  target_compile_features(elona_plugins PUBLIC cxx_std_17)
  if(ELONA_PLUGINS_SYSTEM_ZLIB)
    find_package(ZLIB REQUIRED)
    target_link_libraries(elona_plugins PRIVATE ZLIB::ZLIB)
  else()
    _elona_plugins_pinned_zlib()
    target_link_libraries(elona_plugins PRIVATE elona_zlib)
  endif()
  if(MSVC)
    target_compile_options(elona_plugins PRIVATE /utf-8 /W4)
    target_compile_definitions(elona_plugins PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  else()
    target_compile_options(elona_plugins PRIVATE -Wall -Wextra -Wconversion -Wno-sign-conversion)
  endif()

  if(ARG_TESTS)
    add_executable(elona_plugins_tests
      "${ELONA_PLUGINS_ROOT}/tests/plugins/plugin_tests.cpp"
      "${ELONA_PLUGINS_ROOT}/tests/plugins/hspda_tests.cpp"
      "${ELONA_PLUGINS_ROOT}/tests/plugins/replay_tests.cpp")
    target_link_libraries(elona_plugins_tests PRIVATE elona_plugins)
    if(ELONA_PLUGINS_SYSTEM_ZLIB)
      target_link_libraries(elona_plugins_tests PRIVATE ZLIB::ZLIB)
    else()
      target_link_libraries(elona_plugins_tests PRIVATE elona_zlib)
    endif()
    if(MSVC)
      target_compile_options(elona_plugins_tests PRIVATE /utf-8)
      target_compile_definitions(elona_plugins_tests PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    endif()
    # Fixture data recorded from the original x86 plugins (tests/plugins/reference).
    target_compile_definitions(elona_plugins_tests PRIVATE
      ELONA_PLUGINS_REFERENCE_DIR="${ELONA_PLUGINS_ROOT}/tests/plugins/reference")
    if(COMMAND add_test)
      add_test(NAME plugins_unit
        COMMAND elona_plugins_tests --work "${CMAKE_BINARY_DIR}/plugin-tests")
    endif()
  endif()
endfunction()
