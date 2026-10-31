// Save states, rewind history and run-ahead/rollback support.
//
// Memory is tracked in 4 KB pages (RAM and the disk's settings sectors);
// stores mark pages dirty, and a mirror holds every page as it was at the
// last checkpoint. Checkpoints and rollbacks therefore only copy the pages a
// frame actually touched (~50 of 2184 during a fight).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void state_init(void);              // once the machine is set up, before the first frame

// Mark the current state as the new checkpoint. Call after each emulated
// frame; with history enabled the previous checkpoint is kept for rewind.
void state_checkpoint(void);
// Return to the last checkpoint, discarding everything run since.
void state_rollback(void);

// Rewind history: memory budget in bytes (0 disables it and frees the history).
void state_set_history(size_t bytes);
// Step back one checkpoint. False when the history is empty.
bool state_rewind(void);
double state_history_seconds(void);
size_t state_history_bytes(void);

bool state_save_file(const char *path);
bool state_load_file(const char *path);   // also clears the rewind history

uint64_t state_hash(void);          // of the full machine state, for determinism tests
uint64_t state_quick_hash(void);    // CPU/devices + the game's variables (cheap)
uint64_t state_compat_id(void);     // equal for builds that emulate identically
