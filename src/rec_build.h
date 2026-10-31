// Building the translated game code on the player's machine.
//
// Distributed builds contain no game code. On first launch the program boots
// the player's own game files on the interpreter, snapshots RAM once the
// game has loaded its code, translates that code to C (recomp.c) and compiles
// it with the best compiler available: a bundled Zig, a system gcc/clang, or
// TinyCC (libtcc) as the always-available fallback. The result is cached.
#pragma once
#include <stdbool.h>
#include <stddef.h>

// Progress for a "preparing" screen: stage text and 0..1.
typedef void (*rec_progress_fn)(const char *stage, double fraction);

// Load a module built earlier, if there is a usable one for this game data.
bool rec_build_load_cached(char *msg, size_t msglen);
// Build (snapshot, translate, compile) and load. Slow: tens of seconds.
bool rec_build(rec_progress_fn progress, char *msg, size_t msglen);
// Load a module file directly (testing).
bool rec_build_load_file(const char *path, char *err, size_t errlen);
