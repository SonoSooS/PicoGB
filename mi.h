#pragma once

#include "types.h"


typedef word(*pmiDispatch)(void* userdata, word addr, word data, word type);
typedef const r8* (*pmiDispatchBank)(void* userdata, word addr, word bank);

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
    pmiDispatchBank dispatch_ROM_Bank;
    
    r8 ROM_MAPPER;
};

static inline void mi_params_from_header(struct mi_dispatch* mi, const r8* __restrict ROM)
{
    mi->ROM_MAPPER = ROM[0x147];
}
