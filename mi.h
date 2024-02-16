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

#define MI_LONGDISPATCH_READ_32 0
#define MI_LONGDISPATCH_WRITE_32 1
#define MI_LONGDISPATCH_READ_8 2
#define MI_LONGDISPATCH_WRITE_8 3
#define MI_LONGDISPATCH_BOOTROM_LOCK 255

typedef word(*pmiDispatch)(void* userdata, word addr, word data, word type);
typedef const r8* (*pmiDispatchBank)(void* userdata, word addr, word bank);
typedef r32(*pmiDispatchLong)(void* userdata, r32 addr, r32 data, word type);


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
    #if !CONFIG_ENABLE_LRU
    const r8* __restrict ROMBASE;
    #endif
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
    
    r8 ROM_MAPPER;
    void* userdata;
    pmiDispatch dispatch_ROM;
    pmiDispatch dispatch_IO;
    
#if CONFIG_ENABLE_LRU
    void* userdata_ROM_Bank;
    pmiDispatchBank dispatch_ROM_Bank;
#endif
    
#if CONFIG_BOOTMEME
    pmiDispatchLong dispatch_BOOTROM;
    const void* __restrict userdata_BOOTROM;
    r8 BOOTROM_DATA[8];
#endif
};

PGB_FUNC static inline void mi_params_from_header(struct mi_dispatch* mi, const r8* __restrict ROM)
{
    mi->ROM_MAPPER = ROM[0x147];
}

#if CONFIG_BOOTMEME
PGB_FUNC static r32 mi_iboot_get_offset(const struct mi_dispatch* mi)
{
    return 0
        | (mi->BOOTROM_DATA[0] << 0)
        | (mi->BOOTROM_DATA[1] << 8)
        | (mi->BOOTROM_DATA[2] << 16)
        | (mi->BOOTROM_DATA[3] << 24)
        ;
}

PGB_FUNC static r32 mi_iboot_get_data(const struct mi_dispatch* mi)
{
    return 0
        | (mi->BOOTROM_DATA[4] << 0)
        | (mi->BOOTROM_DATA[5] << 8)
        | (mi->BOOTROM_DATA[6] << 16)
        | (mi->BOOTROM_DATA[7] << 24)
        ;
}

PGB_FUNC static void mi_iboot_set_data(struct mi_dispatch* mi, r32 data)
{
    mi->BOOTROM_DATA[4] = (data >> 0) & 0xFF;
    mi->BOOTROM_DATA[5] = (data >> 8) & 0xFF;
    mi->BOOTROM_DATA[6] = (data >> 16) & 0xFF;
    mi->BOOTROM_DATA[7] = (data >> 24) & 0xFF;
}
#endif
