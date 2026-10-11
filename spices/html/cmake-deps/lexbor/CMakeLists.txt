# spices/html/cmake-deps/lexbor -- the HTML subset of lexbor as one archive.
#
# lexbor is a family of modules (core, dom, html, css, selectors, style,
# encoding, url, unicode, ...).  The html spice needs only the HTML parser and
# the DOM it builds, and lexbor's own html/config.cmake lists exactly four
# dependencies for that: core, dom, ns and tag.  Building upstream's
# all-modules liblexbor would compile ~390k lines (the encoding tables alone
# are 318k) to use ~37k of them, on every CI leg, macOS included.
#
# So this shim fetches the sources without running lexbor's CMakeLists
# (SOURCE_SUBDIR names a directory with no CMakeLists.txt, which makes
# FetchContent_MakeAvailable populate only) and compiles the five modules plus
# the OS port layer into one static target, `lexbor_html`.  The globbing
# mirrors upstream's GET_MODULE_RESURSES macro in config.cmake.
#
# Declared in build.tur as a :path cmake-dep with :targets ["lexbor_html"].
#
# Version pin: v3.0.1 (2026-09-07).
cmake_minimum_required(VERSION 3.20)
project(tur_lexbor_shim C)

include(FetchContent)

FetchContent_Declare(lexbor_upstream
  GIT_REPOSITORY https://github.com/lexbor/lexbor
  GIT_TAG        v3.0.1
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  __tur_populate_only__
)
FetchContent_MakeAvailable(lexbor_upstream)

set(_lxb_src "${lexbor_upstream_SOURCE_DIR}/source")
if(WIN32)
  set(_lxb_port windows_nt)
else()
  set(_lxb_port posix)
endif()

set(_lxb_sources)
foreach(_m core dom ns tag html)
  file(GLOB_RECURSE _m_src "${_lxb_src}/lexbor/${_m}/*.c")
  list(APPEND _lxb_sources ${_m_src})
  set(_port_dir "${_lxb_src}/lexbor/ports/${_lxb_port}/lexbor/${_m}")
  if(IS_DIRECTORY "${_port_dir}")
    file(GLOB_RECURSE _p_src "${_port_dir}/*.c")
    list(APPEND _lxb_sources ${_p_src})
  endif()
endforeach()

add_library(lexbor_html STATIC ${_lxb_sources})
target_include_directories(lexbor_html PUBLIC "${_lxb_src}")
# LEXBOR_STATIC only changes LXB_API on Windows (dllimport vs nothing); it is
# public so a consumer's declarations agree with the archive there too.
target_compile_definitions(lexbor_html PUBLIC LEXBOR_STATIC)
set_target_properties(lexbor_html PROPERTIES
  C_STANDARD 99
  POSITION_INDEPENDENT_CODE ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # upstream ports/posix/config.cmake: clock_gettime and friends.
  target_compile_definitions(lexbor_html PRIVATE _POSIX_C_SOURCE=199309L)
endif()
