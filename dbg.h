#pragma once

#include "config.h"


#if CONFIG_DBG
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DBGF(fmt, ...) if(_IS_DBG)printf(fmt, __VA_ARGS__)
#define DBGS(s) if(_IS_DBG)puts(s)

extern var _IS_DBG;
#else
#define DBGF(fmt, ...)
#define DBGS(s)
#endif
