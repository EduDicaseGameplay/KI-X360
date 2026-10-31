#pragma once
/* VS2010 has no stdbool.h; C++ has bool natively. */
#ifndef __cplusplus
typedef unsigned char bool;
#define true 1
#define false 0
#endif
