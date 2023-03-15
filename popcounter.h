#pragma once


#include "types.h"


typedef struct popcounter
{
    var current;
    var reload;
} popcounter_t;


static inline void popcounter_reset(popcounter_t* __restrict dt)
{
    dt->cur = dt->reload;
}

static inline wbool popcounter_advance(popcounter_t* __restrict dt)
{
    vbool ret = dt->cur & 1;
    
    dt->cur >>= 1;
    if(!dt->cur)
        dt->cur = dt->reload;
    
    return ret;
}
