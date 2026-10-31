// Two-player rollback netplay over UDP.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Runs one frame with these player inputs (bits per BTN_*) and checkpoints it.
// resim: a rollback is replaying an earlier frame (its audio was already heard).
typedef void (*np_run_fn)(const uint16_t in[2], bool resim);
// Power-cycle and run the first NP_BOOT_FRAMES with no input (identical on both sides).
typedef void (*np_reset_fn)(void);

#define NP_BOOT_FRAMES 1000      // to the title logo, where coins start to count
#define NP_DEFAULT_PORT 7845

void netplay_set_callbacks(np_run_fn run, np_reset_fn reset);
bool netplay_host(int port, int input_delay);
bool netplay_join(const char *host, int port, int input_delay);
void netplay_stop(void);

// Call once per display frame. Handles connecting; once running, returns the
// number of new frames emulated (0 while waiting for the opponent).
int  netplay_update(uint16_t local_input);

bool netplay_active(void);       // connecting or playing
bool netplay_running(void);      // playing
int  netplay_local_player(void); // 0 = player 1 (host), 1 = player 2
const char *netplay_status(void);
// Set when the session ended on its own (disconnect, desync, mismatch); cleared when read.
const char *netplay_take_error(void);

// Testing: stop emulating at `frame` (the session keeps confirming inputs).
void netplay_set_end(uint32_t frame);
uint32_t netplay_confirmed(void);   // every frame below this used the real remote input
uint32_t netplay_input_frame(void); // the frame the next local input will be used in
