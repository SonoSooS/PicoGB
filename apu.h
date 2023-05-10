#pragma once

#include "types.h"

#define APU_N_PER_TICK CONFIG_APU_N_PER_TICK

struct apu_ch_t
{
    r16 ctr;
    r16 _unused;
    r8 NR_RAW[5];
    r8 sample_type;
    r8 sample_no;
    r8 sweep_ctr;
    r8 length_ctr;
    r8 vol;
    r16 raw;
};

struct apu_t
{
    struct apu_ch_t ch[4];
    
    var MASTER_CFG;
    var CTR_INT;
    var CTR_INT_FRAC;
    var CTR_DIV;
    var outbuf_pos;
    
    r8 WVRAM[16];
    
    s16 outbuf[2*APU_N_PER_TICK*32768];
};

typedef struct apu_t apu_t;


void apu_reset(apu_t* __restrict pp);
void apu_initialize(apu_t* __restrict pp);
void apu_write(apu_t* __restrict pp, word addr, word data);
word apu_read(apu_t* __restrict pp, word addr);
void apu_write_wave(apu_t* __restrict pp, word addr, word data);
word apu_read_wave(apu_t* __restrict pp, word addr);

void apu_render(apu_t* __restrict pp, s16* outbuf, word ncounts);
void apu_tick_internal_internals(apu_t* __restrict pp);
void apu_tick_internal(apu_t* __restrict pp);

PGB_FUNC static inline void apu_tick(apu_t* __restrict pp, word ncycles, wbool render)
{
    if(pp->MASTER_CFG & (1 << 23))
    {
        pp->CTR_INT_FRAC += ncycles;
        
        while(pp->CTR_INT_FRAC >= APU_N_PER_TICK)
        {
            pp->CTR_INT_FRAC -= APU_N_PER_TICK;
            
            if(!render)
                apu_tick_internal_internals(pp);
            else
                apu_tick_internal(pp);
            
            pp->CTR_INT += APU_N_PER_TICK;
            pp->CTR_DIV += APU_N_PER_TICK;
        }
    }
    else
    {
        pp->CTR_DIV += ncycles;
    }
}
