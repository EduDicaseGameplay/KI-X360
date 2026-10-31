/* Xbox 360 XDK has no intrin.h (zstd in libchdr includes it under _MSC_VER).
 * PowerPC intrinsics come from ppcintrinsics.h; the x86 bit scans zstd uses
 * are built from cntlzw/cntlzd. */
#pragma once
#include <ppcintrinsics.h>

static __inline unsigned char _BitScanReverse(unsigned long *index, unsigned long v)
{
    if (!v) return 0;
    *index = 31 - _CountLeadingZeros(v);
    return 1;
}
static __inline unsigned char _BitScanForward(unsigned long *index, unsigned long v)
{
    if (!v) return 0;
    *index = 31 - _CountLeadingZeros(v & (0 - v));      /* lowest set bit */
    return 1;
}
static __inline unsigned char _BitScanReverse64(unsigned long *index, unsigned __int64 v)
{
    if (!v) return 0;
    *index = 63 - (unsigned long)_CountLeadingZeros64(v);
    return 1;
}
static __inline unsigned char _BitScanForward64(unsigned long *index, unsigned __int64 v)
{
    if (!v) return 0;
    *index = 63 - (unsigned long)_CountLeadingZeros64(v & (0 - v));
    return 1;
}
