#pragma once

//------------------------------------------------------------------------------
// Debug
//------------------------------------------------------------------------------

#include "tinyprintf.h"

#if DEBUG
#define dprintf printf
#else
#define dprintf(...)
#endif

void hda_debug_init(void);
