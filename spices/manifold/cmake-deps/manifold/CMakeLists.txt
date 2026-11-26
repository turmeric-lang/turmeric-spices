# spices/manifold/cmake-deps/manifold -- manifold's C binding, plus the C++
# runtime it needs on the link line.
#
# Manifold is C++17 with a C API (`manifoldc`).  Fetching it directly as a
# :url cmake-dep builds fine, but the program then fails to link:
#
#   undefined reference to `operator new(unsigned long)'
#   undefined reference to `__cxa_begin_catch'
#
# tur links emitted C with `cc`, and nothing puts the C++ runtime on that
# line: the walk over INTERFACE_LINK_LIBRARIES finds libmanifoldc.a,
# libmanifold.a, libClipper2.a and -lm, and stops there.  The runtime is a
# property of the C++ *toolchain*, not of any target, so no target reports it.
#
# `:link-flags ["-lstdc++"]` would fix Linux and break macOS, whose runtime is
# libc++, and build.tur has no per-platform key.  So this file asks CMake which
# runtime the C++ compiler links, and records it on manifoldc itself -- tur
# reads a bare INTERFACE_LINK_LIBRARIES entry as `-l<name>`.
#
# It has to be manifoldc, not an INTERFACE wrapper target listed in :targets:
# tur emits $<TARGET_FILE_DIR:t> for every target in :targets, which is a
# hard generate-time error on an INTERFACE library.
cmake_minimum_required(VERSION 3.20)
project(manifold_deps CXX)

include(FetchContent)

# Static archives.  Manifold's own default is BUILD_SHARED_LIBS=ON.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# Otherwise manifold fetches googletest and builds its tests, samples and
# extras alongside the library.
set(MANIFOLD_TEST OFF CACHE BOOL "" FORCE)
set(MANIFOLD_PYBIND OFF CACHE BOOL "" FORCE)
# Single-threaded, on purpose (README "Threading").  MANIFOLD_PAR=ON needs
# oneTBB, and manifold links it as $<$<BOOL:${MANIFOLD_PAR}>:TBB::tbb>.  tur's
# link walk drops any entry that is still a generator expression, so TBB would
# never reach the link line and the build would fail on undefined tbb symbols.
set(MANIFOLD_PAR OFF CACHE BOOL "" FORCE)
# Under Emscripten manifold turns its JavaScript binding on by default; this
# spice wants only the C one.
set(MANIFOLD_JSBIND OFF CACHE BOOL "" FORCE)

# The C binding needs MANIFOLD_CROSS_SECTION (default ON), whose backend is
# Clipper2.  Manifold fetches Clipper2 itself, at a commit it pins, when the
# system has none.
FetchContent_Declare(manifold
  GIT_REPOSITORY https://github.com/elalish/manifold
  GIT_TAG        v3.5.5
)
FetchContent_MakeAvailable(manifold)

# CMAKE_CXX_IMPLICIT_LINK_LIBRARIES is what the C++ compiler driver adds on its
# own: stdc++;m;gcc_s;gcc;c;... with GNU, c++ with Apple clang.  Only the C++
# runtime is missing from a `cc` link; the rest cc adds itself.
foreach(_lib IN LISTS CMAKE_CXX_IMPLICIT_LINK_LIBRARIES)
  if(_lib MATCHES "^(stdc\\+\\+|c\\+\\+|c\\+\\+abi)$")
    set_property(TARGET manifoldc APPEND PROPERTY INTERFACE_LINK_LIBRARIES "${_lib}")
  endif()
endforeach()
