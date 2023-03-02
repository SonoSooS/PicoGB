#pragma once

#include "types.h"
#include "microcode.h"
#include "ppu.h"
#include "apu.h"

#define USE_UD struct pgf_userdata_t* __restrict ud = (struct pgf_userdata_t* __restrict)userdata;

struct pgf_userdata_t
{
    struct mb_state* __restrict mb;
    struct ppu_t* __restrict ppu;
    struct apu_t* __restrict apu;
    void* _resvd;
    
    var JOYP_RAW; // A is $01, Right is $10
    var JOYP;
    
    var TIMER_SUB;
    var TIMER_CNT;
    var TIMER_ACCUM;
    var TIMER_LOAD;
};


void pgf_timer_update_internal(struct pgf_userdata_t* __restrict ud, word ticks);
word pgf_cb_ROM_(void* userdata, word addr, word data, word type);
word pgf_cb_IO_(void* userdata, word addr, word data, word type);

#if CONFIG_ENABLE_LRU
const r8* pgf_cb_ROM_LRU_(void* userdata, word addr, word bank);
#endif

static inline void pgf_timer_update(struct pgf_userdata_t* __restrict ud, word ticks)
{
    if(!(ud->TIMER_CNT & 4))
        return;
    
    pgf_timer_update_internal(ud, ticks);
}
