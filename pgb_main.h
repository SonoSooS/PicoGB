#pragma once


#include "microcode.h"


static inline word pgb_main_tick(mb_state* __restrict mb, vbool* ticked)
{
    word cycles = 1;
    
    if(!mb->HALTING || mbh_irq_get_pending(mb))
    {
        if(mb->HALTING)
            mb->HALTING = 0;
        
        cycles = mb_exec(mb);
        
        if(ticked)
            *ticked = 1;
    }
    else if(mb->HALTING)
    {
        if(!mb->IR.high)
            mb->IR.high = 1
        
        if(ticked)
            *ticked = 0;
    }
    else // ???
    {
        if(ticked)
            *ticked = 0;
    }
    
    return cycles;
}
