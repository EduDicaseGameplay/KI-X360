#pragma once
/* VS2010 C++ has bool natively; C mode needs a shim. */
#ifndef __cplusplus
typedef unsigned char bool;
#define true 1
#define false 0
#endif
