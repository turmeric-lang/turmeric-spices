# spices/xml/cmake-deps/expat -- libexpat as a CMake target tur can link.
#
# libexpat keeps its CMakeLists.txt in the expat/ subdirectory of the
# repository, not at the root, so a plain :url cmake-dep finds nothing to
# add_subdirectory() and no `expat` target ever exists.  This shim fetches the
# repository itself and points SOURCE_SUBDIR at expat/.
#
# It is declared in build.tur as a :path cmake-dep with :targets ["expat"], so
# tur reads the include dir from the target's INTERFACE_INCLUDE_DIRECTORIES
# and links $<TARGET_FILE:expat> -- which matters here, because the archive is
# libexpatd.a in a Debug build, not the libexpat.a a name-derived -l assumes.
#
# Version pin: R_2_9_0 (2026-10-05).  It carries the CVE-2026-102633 and
# CVE-2026-77214 fixes, and 2.4.0+ is what provides the billion-laughs
# amplification limits the xml spice turns on by default.  XML_GE and XML_DTD
# stay ON (expat's defaults): the amplification API is only compiled in with
# XML_GE, and the spice refuses external entities itself rather than by
# compiling DTD support out.
cmake_minimum_required(VERSION 3.20)
project(tur_expat_shim C)

include(FetchContent)

set(EXPAT_BUILD_TOOLS      OFF CACHE BOOL "" FORCE)
set(EXPAT_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(EXPAT_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
set(EXPAT_BUILD_DOCS       OFF CACHE BOOL "" FORCE)
set(EXPAT_BUILD_FUZZERS    OFF CACHE BOOL "" FORCE)
set(EXPAT_BUILD_PKGCONFIG  OFF CACHE BOOL "" FORCE)
set(EXPAT_SHARED_LIBS      OFF CACHE BOOL "" FORCE)
set(EXPAT_ENABLE_INSTALL   OFF CACHE BOOL "" FORCE)

FetchContent_Declare(expat_upstream
  GIT_REPOSITORY https://github.com/libexpat/libexpat
  GIT_TAG        R_2_9_0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  expat
)
FetchContent_MakeAvailable(expat_upstream)

# The spice's glue (c/xml/turxml.c) is linked into position-independent
# shared libraries by `tur repl` and the notebook, so the archive must be PIC.
set_target_properties(expat PROPERTIES POSITION_INDEPENDENT_CODE ON)
