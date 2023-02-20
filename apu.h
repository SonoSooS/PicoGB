#pragma once

#include "types.h"



struct apu_ch_t
{
    r16 ctr;
    r16 reload;
    r8 sample_no;
    r8 sample_type;
    r8 NR_RAW[5];
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
    var CTR_DIV;
    var outbuf_pos;
    
    r8 WVRAM[16];
    
    s16 outbuf[2*8*32768];
};

typedef struct apu_t apu_t;


void apu_reset(apu_t* __restrict pp);
void apu_initialize(apu_t* __restrict pp);
void apu_write(apu_t* __restrict pp, word addr, word data);
word apu_read(apu_t* __restrict pp, word addr);
void apu_write_wave(apu_t* __restrict pp, word addr, word data);
word apu_read_wave(apu_t* __restrict pp, word addr);

void apu_tick_internal(apu_t* __restrict pp);

static inline void apu_tick(apu_t* __restrict pp, word ncycles)
{
    if(pp->MASTER_CFG & (1 << 23))
    {
        word nloops = ((pp->CTR_INT & 7) + ncycles) / 8;
        
        while(nloops--)
        {
            apu_tick_internal(pp);
            
            pp->CTR_INT += 8;
            pp->CTR_DIV += 8;
        }
        
        pp->CTR_INT += ncycles & 7;
        pp->CTR_DIV += ncycles & 7;
    }
    else
    {
        pp->CTR_DIV += ncycles;
    }
}
