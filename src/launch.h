// Started by a launcher (LaunchBox, Playnite, ES-DE, ...) with a game's path:
// a MAME set's zip, its CHD, or a folder holding the files.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "game.h"

// Which game the path holds, and where its files are. ROMs inside zips are
// unpacked once into the cache folder. NULL (and a message in err) if
// neither game's files are there.
const game_t *launch_open(const char *path, char *err, size_t n);
bool launch_active(void);                 // launch_open succeeded
const char *launch_dir(int i);            // folders to look in first, NULL after the last
const char *launch_version(void);         // the only version the launched zip holds, else NULL
