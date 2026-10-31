// The game's own operator adjustments, stored in the disk's settings sectors.
#pragma once
#include <stdbool.h>

// CPU difficulty 1 (extra easy) - 5 (extra hard), as in the operator menu;
// -1 if the adjustment block is missing or its checksum is wrong.
int  adjust_difficulty(void);
// Change it the way the operator menu does. The game reads it at power-on.
bool adjust_set_difficulty(int level);
