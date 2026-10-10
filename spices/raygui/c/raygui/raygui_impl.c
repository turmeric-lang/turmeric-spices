/* The single translation unit that compiles raygui's implementation.
 * Everything else in the spice includes <raygui.h> for declarations only.
 *
 * raygui.h beside this file is raygui 4.0 (raysan5/raygui tag 4.0, commit
 * 25c8c65a6e5f0f4d4b564a0343861898c6f2778b), vendored verbatim under its
 * zlib license (LICENSE).  Bumping it means replacing that file and this
 * note -- nothing else pins the version.
 *
 * tur compiles this TU with the program's -Wall, under which upstream's
 * unchecked fgets/fread calls warn on every build of every raygui program;
 * the pragma keeps the vendored header verbatim. */
#include <raylib.h>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
#endif
#define RAYGUI_IMPLEMENTATION
#include <raygui.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
