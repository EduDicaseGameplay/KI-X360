// Started by a launcher with a game's path. Launchers pass what is in the
// player's MAME folder: roms/kinst2.zip (merged, split or non-merged set),
// roms/kinst2/kinst2.chd, or a folder. Zipped files are found by CRC, so
// renamed ones and other sets' layouts work too; they are unpacked once into
// cache/roms/<set>/ under the profile's names, and the rest of the program
// finds them there like files in run/.
#include "launch.h"
#include "machine.h"
#ifndef UC_NO_SDL
#include <SDL2/SDL.h>     // SDL_strcasecmp (x360_compat.h supplies it without SDL)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#define make_dir(p) mkdir(p, 0755)
#endif

// the same miniz configuration as libchdr's build (third_party/libchdr_all.c)
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_DEFLATE_APIS
#include "third_party/libchdr/deps/miniz-3.1.2/miniz.h"

#define FILE_SIZE 0x80000       // every ROM: boot and sound
#define MAX_ZIPS 6

typedef struct { mz_zip_archive z; FILE *f; bool launched; } zip_t;

static zip_t zips[MAX_ZIPS];
static int nzips;
static char dirs[3][4200];
static int ndirs;
static const char *version;
static bool active;

static size_t zip_read(void *f, mz_uint64 ofs, void *buf, size_t n)
{
    return fseek((FILE *)f, (long)ofs, SEEK_SET) ? 0 : fread(buf, 1, n, (FILE *)f);
}

static void zip_add(const char *path, bool launched)
{
    if (nzips == MAX_ZIPS) return;
    zip_t *z = &zips[nzips];
    memset(z, 0, sizeof *z);
    if (!(z->f = fopen(path, "rb"))) return;
    fseek(z->f, 0, SEEK_END);
    long size = ftell(z->f);
    z->z.m_pRead = zip_read;
    z->z.m_pIO_opaque = z->f;
    if (size <= 0 || !mz_zip_reader_init(&z->z, (mz_uint64)size, 0)) { fclose(z->f); return; }
    z->launched = launched;
    nzips++;
}

static const char *base_name(const char *p)
{
    const char *s = strrchr(p, '/'), *b = strrchr(p, '\\');
    if (b > s) s = b;
    return s ? s + 1 : p;
}

// The member with this CRC (and size), else one with this name in any folder; -1 if none.
static int zip_find(zip_t *z, uint32_t crc, mz_uint64 size, const char *name)
{
    int by_name = -1;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&z->z); i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z->z, i, &st) || st.m_is_directory) continue;
        if (size && st.m_crc32 == crc && st.m_uncomp_size == size) return (int)i;
        if (name && by_name < 0 && !SDL_strcasecmp(base_name(st.m_filename), name)) by_name = (int)i;
    }
    return by_name;
}

static bool exists(const char *path)
{
    struct stat st;
    return !stat(path, &st);
}

static size_t write_out(void *f, mz_uint64 ofs, const void *buf, size_t n)
{
    (void)ofs;
    return fwrite(buf, 1, n, (FILE *)f);
}

// Unpack a member to path unless a file of its size is already there (a ROM
// also needs the same CRC: the zip may have been updated).
static bool unpack(zip_t *z, int idx, const char *path)
{
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&z->z, (mz_uint)idx, &st)) return false;
    FILE *f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        bool same = (mz_uint64)ftell(f) == st.m_uncomp_size;
        if (same && st.m_uncomp_size <= FILE_SIZE) {
            static uint8_t buf[FILE_SIZE];
            fseek(f, 0, SEEK_SET);
            size_t got = fread(buf, 1, sizeof buf, f);
            same = mz_crc32(MZ_CRC32_INIT, buf, got) == st.m_crc32;
        }
        fclose(f);
        if (same) return true;
    }
    if (st.m_uncomp_size > FILE_SIZE) fprintf(stderr, "launch: unpacking %s (%llu MB), once\n", st.m_filename,
                                              (unsigned long long)(st.m_uncomp_size >> 20));
    char tmp[4500];
    snprintf(tmp, sizeof tmp, "%s.part", path);
    if (!(f = fopen(tmp, "wb"))) return false;
    bool ok = mz_zip_reader_extract_to_callback(&z->z, (mz_uint)idx, write_out, f, 0);
    ok &= fclose(f) == 0;
    remove(path);
    if (ok && !rename(tmp, path)) return true;
    remove(tmp);
    fprintf(stderr, "launch: could not unpack %s to %s\n", st.m_filename, path);
    return false;
}

// How surely the launcher's path is game g: its boot ROMs in the launched zip
// decide, then the name (kinst2.chd, a folder kinst2), then its files nearby
// (MAME's folder holds both games). in_launched[v]: version v is in the zip.
static int score(const game_t *g, const char *stem, bool *in_launched, int *files)
{
    int s = SDL_strcasecmp(stem, g->set) ? 0 : 100;
    *files = 0;
    for (int v = 0; v < 4 && g->versions[v].label; v++) {
        in_launched[v] = false;
        for (int i = 0; i < nzips; i++)
            if (zip_find(&zips[i], g->versions[v].crc, FILE_SIZE, NULL) >= 0) {
                s += zips[i].launched ? 1000 : 1;
                ++*files;
                in_launched[v] |= zips[i].launched;
            }
        for (int d = 0; d < ndirs; d++) {
            char p[sizeof dirs + 256];
            snprintf(p, sizeof p, "%s%s", dirs[d], g->versions[v].rom);
            if (exists(p)) { s++; ++*files; }
        }
    }
    return s;
}

const game_t *launch_open(const char *path, char *err, size_t n)
{
    struct stat st;
    if (stat(path, &st)) { snprintf(err, n, "%s: not found", path); return NULL; }
    // the folder it is in (or the folder itself) and the one above: MAME keeps
    // the CHD in roms/<set>/ beside roms/<set>.zip
    char d[4200];
    snprintf(d, sizeof d, "%s", path);
    size_t len = strlen(d);
    if (S_ISDIR(st.st_mode)) {
        if (len && d[len - 1] != '/' && d[len - 1] != '\\') snprintf(d + len, sizeof d - len, "/");
    } else d[base_name(d) - d] = 0;
    char up[4200];
    snprintf(up, sizeof up, "%s", d);
    for (len = strlen(up); len && (up[len - 1] == '/' || up[len - 1] == '\\'); ) up[--len] = 0;
    if (len) up[base_name(up) - up] = 0;
    snprintf(dirs[1], sizeof dirs[1], "%s", *d ? d : "./");
    snprintf(dirs[2], sizeof dirs[2], "%s", *up ? up : *d ? "./" : "../");
    ndirs = 3;
    dirs[0][0] = 0;                               // the unpacked files, below

    const char *name = base_name(path), *dot = strrchr(name, '.');
    char stem[256];
    snprintf(stem, sizeof stem, "%.*s", (int)(dot ? (size_t)(dot - name) : strlen(name)), name);
    if (dot && !SDL_strcasecmp(dot, ".zip")) zip_add(path, true);
    for (int i = 0; game_profiles[i]; i++)
        for (int k = 1; k < 3; k++) {
            char z[4300];
            snprintf(z, sizeof z, "%s%s.zip", dirs[k], game_profiles[i]->set);
            if (exists(z) && SDL_strcasecmp(z, path)) zip_add(z, false);
        }

    const game_t *best = NULL;
    int best_score = 0, files;
    bool in_launched[4], best_in[4];
    for (int i = 0; game_profiles[i]; i++) {
        int s = score(game_profiles[i], stem, in_launched, &files);
        if (files && s > best_score) { best = game_profiles[i]; best_score = s; memcpy(best_in, in_launched, sizeof best_in); }
    }
    if (!best) {
        snprintf(err, n, "%s holds no boot ROM of %s or %s.", path, game_profiles[0]->title, game_profiles[1]->title);
        return NULL;
    }
    int found = 0;
    for (int v = 0; v < 4 && best->versions[v].label; v++)
        if (best_in[v]) { found++; version = best->versions[v].label; }
    if (found != 1) version = NULL;

    // unpack what the zips hold into cache/roms/<set>/
    if (nzips) {
        char dir[4000];
        snprintf(dir, sizeof dir, "%scache", data_dir()); make_dir(dir);
        snprintf(dir, sizeof dir, "%scache/roms", data_dir()); make_dir(dir);
        snprintf(dir, sizeof dir, "%scache/roms/%s", data_dir(), best->set); make_dir(dir);
        snprintf(dirs[0], sizeof dirs[0], "%s/", dir);
        typedef struct { const char *name; uint32_t crc; } wanted_t;
        wanted_t want[16];
        int nwant = 0;
        for (int v = 0; v < 4 && best->versions[v].label; v++) { wanted_t w_ = { best->versions[v].rom, best->versions[v].crc }; want[nwant++] = w_; }
        for (int i = 0; i < 8; i++) { wanted_t w_ = { best->sound_roms[i], best->sound_crc[i] }; want[nwant++] = w_; }
        { wanted_t w_ = { best->chd, 0 }; want[nwant++] = w_; }      // non-standard zips with the disk inside
        { wanted_t w_ = { best->disk, 0 }; want[nwant++] = w_; }
        for (int w = 0; w < nwant; w++)
            for (int i = 0; i < nzips; i++) {
                int idx = zip_find(&zips[i], want[w].crc, want[w].crc ? FILE_SIZE : 0, want[w].name);
                if (idx < 0) continue;
                char out[4400];
                snprintf(out, sizeof out, "%s%s", dirs[0], want[w].name);
                unpack(&zips[i], idx, out);
                break;
            }
    }
    for (int i = 0; i < nzips; i++) { mz_zip_reader_end(&zips[i].z); fclose(zips[i].f); }
    nzips = 0;
    if (!*dirs[0]) {                              // nothing unpacked: just the two folders
        memmove(dirs[0], dirs[1], sizeof dirs[0] * 2);
        ndirs = 2;
    }
    active = true;
    fprintf(stderr, "launch: %s from %s%s%s\n", best->title, path, version ? ", version " : "", version ? version : "");
    return best;
}

bool launch_active(void) { return active; }
const char *launch_dir(int i) { return active && i >= 0 && i < ndirs ? dirs[i] : NULL; }
const char *launch_version(void) { return version; }
