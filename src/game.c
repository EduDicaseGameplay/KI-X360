// The game profile in use (game.h).
#include "game.h"
#include <string.h>

game_t game;
const game_t *const game_profiles[] = { &game_ki1, &game_ki2, NULL };

void game_select(const game_t *g) { game = *g; }

const game_t *game_find(const char *id)
{
    for (int i = 0; game_profiles[i]; i++)
        if (id && !strcmp(game_profiles[i]->id, id)) return game_profiles[i];
    return NULL;
}
