// Force-included (/FI) into every source when building with the Xbox 360 XDK
// compiler (VS2010 cl, compiling the C sources as C++). Supplies the GNU C and
// C99/C11 library pieces the code relies on that this toolchain lacks.
#pragma once
#if defined(_MSC_VER)

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>

#ifdef _XBOX
#include <ppcintrinsics.h>
// The console has no environment; every debug variable reads as unset, except
// the disk log in diagnostic builds (UC_DIAG).
static inline char *x360_getenv(const char *name)
{
#if defined(UC_DIAG) || defined(UC_LOGEXC)
    static char on[] = "1";
#endif
#ifdef UC_DIAG
    if (strcmp(name, "KI2_LOG_IDE") == 0) return on;
#endif
#ifdef UC_LOGEXC
    if (strcmp(name, "KI2_LOG_EXC") == 0) return on;     // every MIPS exception into the log
#endif
    (void)name;
    return NULL;
}
#define getenv x360_getenv
#define X360_BIG_ENDIAN 1

// The CRT's stderr reaches no file on the console: messages for it go to the
// frontend's log (main360.cpp), which keeps them in memory, shows them and
// writes them out.
#ifdef __cplusplus
extern "C"
#endif
void x360_log_write(const char *s);
static inline int x360_fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    if (f == stderr) {
        char b[1024];
        r = _vsnprintf(b, sizeof b - 1, fmt, ap);
        b[sizeof b - 1] = 0;
        if (r < 0) r = (int)strlen(b);
        x360_log_write(b);
    } else {
        r = vfprintf(f, fmt, ap);
    }
    va_end(ap);
    return r;
}
#define fprintf x360_fprintf
#include <errno.h>
static inline void x360_perror(const char *s) { fprintf(stderr, "%s: %s\n", s ? s : "", strerror(errno)); }
#define perror x360_perror

// The console's file system wants backslashes; the shared code builds paths
// with '/'. These take either.
#include <sys/types.h>
#include <sys/stat.h>
#include <direct.h>
static inline const char *x360_path(const char *p, char *buf, size_t n)
{
    size_t i = 0;
    for (; p[i] && i + 1 < n; i++) buf[i] = p[i] == '/' ? '\\' : p[i];
    buf[i] = 0;
    return buf;
}
static inline FILE *x360_fopen(const char *p, const char *mode)
{ char b[1024]; return fopen(x360_path(p, b, sizeof b), mode); }
static inline int x360_remove(const char *p) { char b[1024]; return remove(x360_path(p, b, sizeof b)); }
static inline int x360_rename(const char *a, const char *z)
{ char b[1024], c[1024]; return rename(x360_path(a, b, sizeof b), x360_path(z, c, sizeof c)); }
static inline int x360_mkdir(const char *p) { char b[1024]; return _mkdir(x360_path(p, b, sizeof b)); }
static inline int x360_stat(const char *p, struct stat *st) { char b[1024]; return stat(x360_path(p, b, sizeof b), st); }
#define fopen x360_fopen
#define remove x360_remove
#define rename x360_rename
#define _mkdir x360_mkdir
#define stat(p, st) x360_stat((p), (st))
#endif

// These builds (the console and its PC test harness) have no SDL: the little
// of it that launch.c and osd.c use.
#define UC_NO_SDL 1
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
static inline uint32_t SDL_GetTicks(void) { return (uint32_t)((uint64_t)clock() * 1000 / CLOCKS_PER_SEC); }
#define SDL_TICKS_PASSED(a, b) ((int32_t)((b) - (a)) <= 0)
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#define SDL_strcasecmp _stricmp

// C99 snprintf: always terminates, returns the length it wanted.
static inline int x360_vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
    int r = n ? _vsnprintf(buf, n, fmt, ap) : -1;
    if (n && (r < 0 || (size_t)r >= n)) buf[n - 1] = 0;
    if (r < 0) r = _vscprintf(fmt, ap);
    return r;
}
static inline int x360_snprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int r = x360_vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}
#define snprintf x360_snprintf
#define vsnprintf x360_vsnprintf

#define __attribute__(x)
#define __typeof__(x) decltype(x)
#define __builtin_memcpy memcpy
#define __builtin_memset memset
#define __builtin_expect(x, v) (x)

// GCC overflow builtins, for the signed 32/64-bit uses in the CPU core.
static inline bool x360_add_ov(int32_t a, int32_t b, int32_t *r)
{ int32_t s = (int32_t)((uint32_t)a + (uint32_t)b); *r = s; return ((a ^ s) & (b ^ s)) < 0; }
static inline bool x360_add_ov(int64_t a, int64_t b, int64_t *r)
{ int64_t s = (int64_t)((uint64_t)a + (uint64_t)b); *r = s; return ((a ^ s) & (b ^ s)) < 0; }
static inline bool x360_sub_ov(int32_t a, int32_t b, int32_t *r)
{ int32_t s = (int32_t)((uint32_t)a - (uint32_t)b); *r = s; return ((a ^ b) & (a ^ s)) < 0; }
static inline bool x360_sub_ov(int64_t a, int64_t b, int64_t *r)
{ int64_t s = (int64_t)((uint64_t)a - (uint64_t)b); *r = s; return ((a ^ b) & (a ^ s)) < 0; }
#define __builtin_add_overflow(a, b, r) x360_add_ov((a), (b), (r))
#define __builtin_sub_overflow(a, b, r) x360_sub_ov((a), (b), (r))

static inline int x360_clz(uint32_t v)
{
#ifdef _XBOX
    return _CountLeadingZeros(v);
#else
    int n = 0; if (!v) return 32; while (!(v & 0x80000000u)) { v <<= 1; n++; } return n;
#endif
}
#define __builtin_clz(v) x360_clz(v)

// 64x64 -> 128-bit unsigned multiply (no __int128 here).
static inline void mul64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo)
{
    uint64_t al = (uint32_t)a, ah = a >> 32, bl = (uint32_t)b, bh = b >> 32;
    uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint64_t mid = (ll >> 32) + (uint32_t)lh + (uint32_t)hl;
    *lo = (mid << 32) | (uint32_t)ll;
    *hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}

// C99 maths missing from this CRT.
#include <math.h>
#include <float.h>
static inline double x360_trunc(double x) { return x < 0 ? ceil(x) : floor(x); }
static inline float x360_truncf(float x) { return x < 0 ? ceilf(x) : floorf(x); }
// Round half to even, as nearbyint does in the default rounding mode.
static inline double x360_nearbyint(double x)
{
    double f = floor(x), d = x - f;
    if (d > 0.5 || (d == 0.5 && fmod(f, 2.0) != 0)) f += 1;
    return f == 0 ? _copysign(0.0, x) : f;
}
#define trunc x360_trunc
#define truncf x360_truncf
#define nearbyint x360_nearbyint
#define isnan(x) (_isnan((double)(x)) != 0)

#endif
