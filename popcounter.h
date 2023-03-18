#pragma once


#include "types.h"


typedef struct popcounter
{
    var current;
    var reload;
} popcounter_t;


static inline void popcounter_reset(popcounter_t* __restrict dt)
{
    dt->current = dt->reload;
}

static inline wbool popcounter_advance(popcounter_t* __restrict dt)
{
    vbool ret = dt->current & 1;
    
    dt->current >>= 1;
    if(!dt->current)
        dt->current = dt->reload;
    
    return ret;
}
