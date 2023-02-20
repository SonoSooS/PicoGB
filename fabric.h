#pragma once

#include "types.h"
#include "microcode.h"
#include "ppu.h"
#include "apu.h"

struct userdata_t
{
    struct mb_state* __restrict mb;
    struct ppu_t* __restrict ppu;
    struct apu_t* __restrict apu;
    void* _resvd;
    
    var JOYP_RAW; // A is $10, Right is $01
    var JOYP;
    
    var TIMER_SUB;
    var TIMER_CNT;
    var TIMER_ACCUM;
    var TIMER_LOAD;
};


void timer_update_internal(struct userdata_t* __restrict ud, word ticks);
word cb_ROM_(void* userdata, word addr, word data, word type);
word cb_IO_(void* userdata, word addr, word data, word type);

static inline void timer_update(struct userdata_t* __restrict ud, word ticks)
{
    if(!(ud->TIMER_CNT & 4))
        return;
    
    timer_update_internal(ud, ticks);
}
