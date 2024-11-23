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
    word _debug;
    
    var JOYP_RAW; // A is $01, Right is $10
    var JOYP;
    
    var TIMER_SUB;
    var TIMER_CNT;
    var TIMER_ACCUM;
    var TIMER_LOAD;
    
    var SB;
    var SC;
    
#if CONFIG_IS_CGB
    var GDMA_SRC;
    var GDMA_DST;
    var GDMA_CNT;
    
    var CGB_SPEED;
    var CGB_MODE;
#endif
};


void pgf_timer_update_internal(struct pgf_userdata_t* __restrict ud, word ticks);
const r8* pgf_resolve_ROM(void* userdata, word addr, word bank);
word pgf_cb_ROM_(void* userdata, word addr, word data, word type);
word pgf_cb_IO_(void* userdata, word addr, word data, word type);

PGB_FUNC word pgf_cb_ROM_Dummy(void* userdata, word addr, word data, word type);
PGB_FUNC word pgf_cb_ROM_MBC1(void* userdata, word addr, word data, word type);
PGB_FUNC word pgf_cb_ROM_MBC2(void* userdata, word addr, word data, word type);
PGB_FUNC word pgf_cb_ROM_MBC3(void* userdata, word addr, word data, word type);
PGB_FUNC word pgf_cb_ROM_MBC5(void* userdata, word addr, word data, word type);

PGB_FUNC pmiDispatch pgf_get_mapper_callback(word mapper_id);

#if CONFIG_BOOTMEME
PGB_FUNC r32 pgf_cb_BOOTROM(void* userdata, r32 addr, r32 data, word type);
#endif
#if CONFIG_ENABLE_LRU
const r8* pgf_cb_ROM_LRU_(void* userdata, word addr, word bank);
#endif

static inline void pgf_timer_update(struct pgf_userdata_t* __restrict ud, word ticks)
{
    if(!(ud->TIMER_CNT & 4))
        return;
    
    pgf_timer_update_internal(ud, ticks);
}

static inline const r8* pgf_resolve_ROM_internal(void* userdata, word addr, word bank)
{
    const r8* res = NULL;
    
    USE_UD;
    
    if(addr < 0x4000)
        bank = 0;

#if CONFIG_ENABLE_LRU    
    if(ud->mb->mi->ROM != NULL)
#endif
    {
#if CONFIG_USE_FLAT_ROM
        res = &ud->mb->mi->ROM[bank * 0x4000];
        return &res[addr & 0x3FFF];
#else
        res = ud->mb->mi->ROM[bank];
        if(res != NULL)
            return &res[addr & 0x3FFF];
#endif
    }
    
#if CONFIG_ENABLE_LRU
    res = ud->mb->mi->dispatch_ROM_Bank(userdata, addr & ~MICACHE_R_SEL, bank);
    if(res != NULL)
        return &res[((addr & 0x3FFF) & ~MICACHE_R_SEL) + (addr & MICACHE_R_SEL)];
#endif
    
    return NULL;
}
