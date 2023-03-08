#pragma once

#include "types.h"

#if CONFIG_ENABLE_LRU
#include "lru.h"
#endif

// Size of cache region in (1 << BITS) size. Valid between 12 to 8 inclusive.
#define MICACHE_R_BITS 12
#define MICACHE_R_RESET 0xFFFF
#define MICACHE_R_SEL ((1 << MICACHE_R_BITS) - 1)
#define MICACHE_R_VALUE(v) ((v) >> MICACHE_R_BITS)


typedef word(*pmiDispatch)(void* userdata, word addr, word data, word type);
typedef const r8* (*pmiDispatchBank)(void* userdata, word addr, word bank);


#if CONFIG_ENABLE_LRU
struct mi_dispatch_ROM_Bank
{
    struct lru_state* lru;
    void* userdata;
    pmiDispatchBank dispatch;
};
#endif

struct mb_mi_cache
{
    const r8* __restrict mc_execute[MICACHE_R_VALUE(0x10000)];
    const r8* __restrict mc_read[MICACHE_R_VALUE(0x10000)];
    r8* __restrict mc_write[MICACHE_R_VALUE(0x10000)];
    void* _mc_dummy;
};

struct mi_dispatch
{
    const r8* __restrict const * __restrict ROM;
    r8* __restrict WRAM;
    r8* __restrict VRAM;
    r8* __restrict SRAM;
    
    var BANK_ROM;
    var BANK_WRAM;
    var BANK_VRAM;
    var BANK_SRAM;
    
    r8* __restrict HRAM;
    r8* __restrict OAM;
    var N_ROM;
    var N_SRAM;
    
    //const r8* __restrict ROM_ORIG;
    void* userdata;
    pmiDispatch dispatch_ROM;
    pmiDispatch dispatch_IO;
    
#if CONFIG_ENABLE_LRU
    void* userdata_ROM_Bank;
    pmiDispatchBank dispatch_ROM_Bank;
#endif
    
    r8 ROM_MAPPER;
};

static inline void mi_params_from_header(struct mi_dispatch* mi, const r8* __restrict ROM)
{
    mi->ROM_MAPPER = ROM[0x147];
}
