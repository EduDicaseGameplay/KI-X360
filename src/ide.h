#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Open the disk image, a raw file or a MAME CHD; it is only read. The game's
// settings sectors are kept in `settings` (written by ide_flush once the game
// changes them); until that file exists they come from the image.
bool ide_open(const char *image, const char *settings, uint32_t cyls, uint32_t heads, uint32_t secs, int serial_offset);
const char *ide_chd_sha1(void);          // the CHD's SHA1 as chdman shows it, NULL for a raw file
uint64_t ide_image_size(void);
bool ide_preload(void);                  // read the whole image into memory now (slow drives)
bool ide_unpack_begin(void);             // a CHD: room to keep it decompressed (false: doesn't fit, or no CHD)
bool ide_unpack_step(void);              // decompress the next block not yet done; false when all are
bool ide_write_raw(const char *path);    // write the image as a raw file
void ide_use_settings(const char *path); // save the current settings file, switch to this one
uint32_t ide_read(int reg);
void ide_write(int reg, uint32_t v);
uint32_t ide_alt_status(void);
void ide_devctl(uint32_t v);
void ide_tick(int cycles);

// The game keeps its settings and audits in the first sectors of the disk.
// Those are tracked like RAM pages, so save states and rewind include them.
#define IDE_TRACK_BYTES (64 * 512)
#define IDE_TRACK_PAGES (IDE_TRACK_BYTES >> 12)
extern uint8_t ide_dirty[IDE_TRACK_PAGES];
uint8_t *ide_tracked(void);              // in-memory copy of the first IDE_TRACK_BYTES
void ide_tracked_changed(void);          // the copy was modified (state load/rewind)
void ide_flush(void);                    // save the copy to the settings file
void ide_power_on(void);                 // controller back to its state after ide_open
uint64_t ide_image_id(void);             // identity of the image (for netplay)

// Save state: copy the controller state to/from buf (NULL: just return the size).
size_t ide_state(uint8_t *buf, bool load);
