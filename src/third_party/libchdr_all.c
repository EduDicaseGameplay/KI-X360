// libchdr as one compilation unit: its own unity.c, plus the A/V codec that
// unity.c leaves out but the reader's codec table refers to. miniz's zip
// reader stays in for launch.c (MAME's zipped sets); no stdio, no writing.
#define MINIZ_ARCHIVE_APIS
#include "libchdr/unity.c"
#include "libchdr/src/libchdr_codec_avhuff.c"
