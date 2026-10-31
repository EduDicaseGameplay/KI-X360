// Choosing which game to play, when both are installed.
#pragma once
#include "game.h"
#include <stdbool.h>

// A window with a tile per game (games[0], games[1]); installed[i]: its
// files were found (a game without them can't be chosen). Returns the
// chosen profile, or NULL if the window was closed.
const game_t *picker_choose(const game_t *const *games, const bool *installed, int initial);
// The first screen when no game's files are there: what to add and where
// (folder; alt "" or a second place). Enter checks again with found().
// 1: the files are there; 0: the window was closed; -1: no window could open.
int picker_welcome(const game_t *const *games, const char *folder, const char *alt, bool (*found)(void));
// A plain window showing text until closed, for when SDL's message box can't
// open (no zenity and no X core fonts). False if no window could be made.
bool picker_message(const char *title, const char *text);
// Whether version v's boot ROM of game g is there (main.c).
bool picker_version_there(const game_t *g, int v);
