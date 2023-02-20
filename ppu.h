#pragma once

#include "types.h"


struct ppu_state_t
{
    pixel_t* __restrict * __restrict framebuffer;
    void* _dummy1;
    void* _dummy2;
    void* _dummy3;
    
    var scanX;
    var scanY;
    var subclk;
    var interlace;
    
    var posX;
    var _dummy5;
    var latchY;
    var latchX;
    
    hilow16_t latches[10];
};

struct ppu_t
{
    const r8* __restrict VRAM;
    const r8* __restrict OAM;
    
    var next_update_ticks;
    var next_update_n;
    
    var is_cgb;
    var _redrawed;
    var rOBPI;
    var rBGPI;
    
    var rOBP0;
    var rOBP1;
    var rBGP;
    var rLYC;
    
    var rLCDC;
    var rSTAT;
    var rSCX;
    var rSCY;
    
    var rWX;
    var rWY;
    var IF_SCHED;
    var _internal_WY;
    
    r16 OBP[4*8];
    r16 BGP[4*8];
    
    struct ppu_state_t state;
};

typedef struct ppu_t ppu_t;


void ppu_reset(ppu_t* __restrict pp);
void ppu_initialize(ppu_t* __restrict pp);
void ppu_turn_off(ppu_t* __restrict pp);
void ppu_turn_on(ppu_t* __restrict pp);

void ppu_tick_internal(ppu_t* __restrict pp, word ncycles, word rem);


static inline word ppu_tick(ppu_t* __restrict pp, word ncycles)
{
    var rem = pp->next_update_ticks;
    
    if(ncycles < rem) // Less cycles than to cause an event
    {
        // Just subtract and return
        pp->next_update_ticks = rem - ncycles;
        return 0;
    }
    
    ppu_tick_internal(pp, ncycles, rem);
    
    var ret = pp->IF_SCHED;
    if(ret)
    {
        pp->IF_SCHED = 0;
    }
    return ret;
}
