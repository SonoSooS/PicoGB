#include "microcode.h"
#include "microcode_dispatch.h"
#include "dbg.h"


#define self mb_state* __restrict

#define USE_MIC struct mb_mi_cache* __restrict mic = &mb->micache;
#define USE_MI struct mi_dispatch* __restrict mi = mb->mi;

//#define R_RELATIVE_ADDR (addr & MICACHE_R_SEL)
#define R_RELATIVE_ADDR (addr - (r_addr << MICACHE_R_BITS))


#pragma region Resolve uncached region + fabric interface

#if CONFIG_ENABLE_LRU
// Resolve an aligned(!) pointer to a ROM bank,
//  based on an input address and current banking settings.
// addr < 0x8000
//TODO: get rid of this
PGB_FUNC static inline const r8* __restrict mch_resolve_mic_bank_internal(const self mb, word r_addr)
{
    USE_MI;
    
    const r8* __restrict ret = mi->dispatch_ROM_Bank(mi->userdata, r_addr << MICACHE_R_BITS, mi->BANK_ROM);
    
    return ret;
}
#endif

PGB_FUNC static r8* __restrict mch_resolve_mic_r_direct(const self mb, word r_addr)
{
    USE_MI;
    
    if(r_addr < MICACHE_R_VALUE(0x8000))
    {
        // ROM area is unpredictable, can't calculate it here
        __builtin_unreachable();
    }
    else if(r_addr < MICACHE_R_VALUE(0xA000))
    {
        r8* __restrict ptr = &mi->VRAM[mi->BANK_VRAM << 13];
        
        r_addr -= MICACHE_R_VALUE(0x8000);
        return &(ptr[r_addr << MICACHE_R_BITS]);
    }
    else if(r_addr < MICACHE_R_VALUE(0xC000))
    {
        r8* __restrict ptr = &mi->SRAM[mi->BANK_SRAM << 13];
        
        r_addr -= MICACHE_R_VALUE(0xA000);
        return &(ptr[r_addr << MICACHE_R_BITS]);
    }
    else // WRAM only [$C000; $FFFF] for OAMDMA
    {
        if(COMPILER_UNLIKELY(r_addr >= MICACHE_R_VALUE(0x10000)))
            __builtin_unreachable();
        
        if(r_addr < MICACHE_R_VALUE(0xE000))
            r_addr -= MICACHE_R_VALUE(0xC000);
        else
            r_addr -= MICACHE_R_VALUE(0xE000);
        
        if(r_addr < MICACHE_R_VALUE(0x1000))
        {
            return &(mi->WRAM[r_addr << MICACHE_R_BITS]);
        }
        else
        {
            var bank = mi->BANK_WRAM;
            if(!bank)
                bank = 1;
            
            r_addr -= MICACHE_R_VALUE(0x1000);
            return &(mi->WRAM[(bank << 12) + (r_addr << MICACHE_R_BITS)]);
        }
    }
}

PGB_FUNC static const r8* __restrict mch_resolve_mic_r_direct_ROM(const self mb, word r_addr)
{
    USE_MI;
    
    const r8* __restrict ret = NULL;
    
    //TODO: optimize this for LRU
#if CONFIG_ENABLE_LRU
    if(mi->ROM != NULL)
#endif
    {
        if(r_addr < MICACHE_R_VALUE(0x4000))
        {
        #if CONFIG_USE_FLAT_ROM
            ret = &mi->ROM[0];
            return &ret[r_addr << MICACHE_R_BITS];
        #else
            ret = mi->ROM[0];
            if(ret != NULL)
                return &ret[r_addr << MICACHE_R_BITS];
        #endif
        }
        else
        {
        #if CONFIG_USE_FLAT_ROM
            r_addr -= MICACHE_R_VALUE(0x4000);
            ret = &mi->ROM[mi->BANK_ROM << 14];
            return &ret[r_addr << MICACHE_R_BITS];
        #else
            ret = mi->ROM[mi->BANK_ROM];
            if(ret != NULL)
            {
                r_addr -= MICACHE_R_VALUE(0x4000);
                return &ret[r_addr << MICACHE_R_BITS];
            }
        #endif
        }
    }
    
#if CONFIG_ENABLE_LRU
    ret = mch_resolve_mic_bank_internal(mb, r_addr);
#endif
    return ret;
}

// Uncached resolve aligned readable const memory area, based on address
PGB_FUNC ATTR_FORCE_NOINLINE static const r8* __restrict mch_resolve_mic_r_direct_read(const self mb, word r_addr)
{
    if(r_addr < MICACHE_R_VALUE(0x8000))
    {
        return mch_resolve_mic_r_direct_ROM(mb, r_addr);
    }
    
    return mch_resolve_mic_r_direct(mb, r_addr);
}

PGB_FUNC ATTR_FORCE_NOINLINE static r8* __restrict mch_resolve_mic_r_direct_write(const self mb, word r_addr)
{
    if(r_addr < MICACHE_R_VALUE(0x8000))
    {
        __builtin_unreachable();
        
        // ROM is *Read-Only* Memory, can't write to it normally
        return NULL;
    }
    
    return mch_resolve_mic_r_direct(mb, r_addr);
}

#pragma endregion

#pragma region Resolve cached memory region (by address)
/*
    All functions in this region: cached memory resolve
    - addr < 0xE000
*/

#pragma region Resolve (read)

PGB_FUNC ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static const r8* __restrict mch_resolve_mic_read_slow(self mb, word addr)
{
    const r8* __restrict ptr;
    
    var r_addr = MICACHE_R_VALUE(addr);
    
    ptr = mch_resolve_mic_r_direct_read(mb, r_addr);
    if(COMPILER_LIKELY(ptr != NULL))
    {
#if !CONFIG_MIC_CACHE_BYPASS
        USE_MIC;
        mic->mc_read[r_addr] = ptr;
#endif
        
        return &ptr[R_RELATIVE_ADDR];
    }
    
    return NULL;
}

PGB_FUNC ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static word mch_resolve_mic_read_slow_deref(self mb, word addr)
{
    return *mch_resolve_mic_read_slow(mb, addr);
}

PGB_FUNC ATTR_HOT __attribute__((optimize("O2"))) static word mch_resolve_mic_read_deref(self mb, word addr)
{
#if !CONFIG_MIC_CACHE_BYPASS
    var r_addr = MICACHE_R_VALUE(addr);
    
    const r8* __restrict ptr;
    
    ptr = mb->micache.mc_read[r_addr];
    if(COMPILER_LIKELY(ptr != NULL))
        return ptr[R_RELATIVE_ADDR];
    else
#endif
    
    return mch_resolve_mic_read_slow_deref(mb, addr);
}

#pragma endregion

#pragma region Resolve (write)

PGB_FUNC ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static void mch_resolve_mic_write_slow_deref(self mb, word addr, word data)
{
    USE_MIC;
    
    var r_addr = MICACHE_R_VALUE(addr);
    
    r8* __restrict ptr;
    ptr = mch_resolve_mic_r_direct_write(mb, r_addr);
    if(COMPILER_LIKELY(ptr != NULL))
    {
        mic->mc_write[r_addr] = ptr;
        ptr[R_RELATIVE_ADDR] = data;
        return;
    }
    
    __builtin_unreachable();
    *ptr = data;
}

PGB_FUNC ATTR_HOT __attribute__((optimize("Os"))) static void mch_resolve_mic_write_deref_helper(r8* __restrict ptr, word addr, word data)
{
    var r_addr = MICACHE_R_VALUE(addr);
    addr = R_RELATIVE_ADDR;
    ptr[addr] = data;
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("O2"))) static void mch_resolve_mic_write_deref(self mb, word addr, word data)
{
#if !CONFIG_MIC_CACHE_BYPASS
    var r_addr = MICACHE_R_VALUE(addr);
    
    r8* __restrict ptr;
    
    ptr = mb->micache.mc_write[r_addr];
    if(ptr != NULL)
    {
        mch_resolve_mic_write_deref_helper(ptr, addr, data); //HACK: prevent register allocator spill, as it's slower than a tail call
        return;
    }
    else
#endif
    
    mch_resolve_mic_write_slow_deref(mb, addr, data);
}

#pragma endregion

#pragma region Resolve (execute)

PGB_FUNC ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static const r8* __restrict mch_resolve_mic_execute_slow(self mb, word addr)
{
    const r8* __restrict ptr;
    
    var r_addr = MICACHE_R_VALUE(addr);
    
    ptr = mch_resolve_mic_r_direct_read(mb, r_addr);
    if(COMPILER_LIKELY(ptr != NULL))
    {
#if !CONFIG_MIC_CACHE_BYPASS
        USE_MIC;
        mic->mc_execute[r_addr] = ptr;
#endif
        
        return &ptr[R_RELATIVE_ADDR];
    }
    
    return NULL;
}

PGB_FUNC ATTR_HOT ATTR_FORCE_INLINE __attribute__((optimize("O2"))) static inline const r8* __restrict mch_resolve_mic_execute(self mb, word addr)
{
    
#if !CONFIG_MIC_CACHE_BYPASS
    var r_addr = MICACHE_R_VALUE(addr);
    
    const r8* __restrict ptr;
    
    ptr = mb->micache.mc_execute[r_addr];
    if(COMPILER_LIKELY(ptr != NULL))
        return &ptr[R_RELATIVE_ADDR];
#endif
    
    return mch_resolve_mic_execute_slow(mb, addr);
}

PGB_FUNC ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static word mch_resolve_mic_execute_slow_deref(self mb, word addr)
{
    return *mch_resolve_mic_execute_slow(mb, addr);
}

PGB_FUNC ATTR_HOT ATTR_FORCE_INLINE __attribute__((optimize("O2"))) static inline word mch_resolve_mic_execute_deref(self mb, word addr)
{
#if !CONFIG_MIC_CACHE_BYPASS
    var r_addr = MICACHE_R_VALUE(addr);
    
    const r8* __restrict ptr;
    
    ptr = mb->micache.mc_execute[r_addr];
    if(COMPILER_LIKELY(ptr != NULL))
        return ptr[R_RELATIVE_ADDR];
    else
#endif
    
    return mch_resolve_mic_execute_slow_deref(mb, addr);
}

#pragma endregion

#pragma endregion

#pragma region Dispatch

#pragma region Dispatch special (IO + ROM)

PGB_FUNC ATTR_FORCE_NOINLINE  __attribute__((optimize("O2"))) static void mch_memory_dispatch_write_ROM(const self mb, word addr, word data)
{
    mb->mi->dispatch_ROM(mb->mi->userdata, addr, data, MB_TYPE_WRITE);
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("O2"))) static word mch_memory_dispatch_read_IO(const self mb, word haddr)
{
    return mb->mi->dispatch_IO(mb->mi->userdata, haddr, MB_DATA_DONTCARE, MB_TYPE_READ);
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE  __attribute__((optimize("O2"))) static void mch_memory_dispatch_write_IO(const self mb, word haddr, word data)
{
    mb->mi->dispatch_IO(mb->mi->userdata, haddr, data, MB_TYPE_WRITE);
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("O2"))) static word mch_memory_dispatch_read_HRAM(const self mb, word haddr)
{
    if(haddr < 0xFF)
        return mb->mi->HRAM[haddr - 0x80];
    else
        return mb->IE;
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) word mch_memory_dispatch_read_Haddr(const self mb, word haddr)
{
    if(haddr < 0x80)
        return mch_memory_dispatch_read_IO(mb, haddr);
    else
        return mch_memory_dispatch_read_HRAM(mb, haddr);
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) void mch_memory_dispatch_write_Haddr(self mb, word haddr, word data)
{
    if(haddr < 0x80)
        mch_memory_dispatch_write_IO(mb, haddr, data);
    else if(haddr < 0xFF)
        mb->mi->HRAM[haddr - 0x80] = data;
    else
        mb->IE = data;
}

PGB_FUNC ATTR_FORCE_NOINLINE __attribute__((optimize("O2"))) static word mch_memory_dispatch_read_fexx_ffxx(const self mb, word addr)
{
    if(addr & 0x100)
        return mch_memory_dispatch_read_Haddr(mb, addr & 0xFF);
    
    return mb->mi->OAM[addr & 0xFF];
}

PGB_FUNC ATTR_FORCE_NOINLINE __attribute__((optimize("O2"))) static void mch_memory_dispatch_write_fexx_ffxx(self mb, word addr, word data)
{
    if(addr & 0x100)
    {
        mch_memory_dispatch_write_Haddr(mb, addr & 0xFF, data);
        return;
    }
    
    mb->mi->OAM[addr & 0xFF] = addr;
}

#pragma endregion

#pragma region Dispatch fetch (1)

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static word mch_memory_fetch_decode_1(self mb, word addr)
{
    if(COMPILER_LIKELY(addr < 0xFE00))
        return mch_resolve_mic_execute_deref(mb, addr);
    else
        return mch_memory_dispatch_read_fexx_ffxx(mb, addr);
}

PGB_FUNC ATTR_HOT word mch_memory_fetch_PC_op_1(self mb)
{
    word addr = mb->PC;
    mb->PC = (addr + 1) & 0xFFFF;
    
    var res = mch_memory_fetch_decode_1(mb, addr);
    DBGF("- /O1 %04X <> %02X\n", addr, res);
    return res;
}

PGB_FUNC ATTR_HOT word mch_memory_fetch_PC(self mb)
{
    word addr = mb->PC;
    mb->PC = (addr + 1) & 0xFFFF;
    
    var res = mch_memory_fetch_decode_1(mb, addr);
    DBGF("- /M1 %04X <> %02X\n", addr, res);
    return res;
}

#pragma endregion

#pragma region Dispatch fetch (2)

PGB_FUNC ATTR_FORCE_NOINLINE __attribute__((optimize("O1"))) static word mch_memory_fetch_decode_2_slow(self mb, word addr)
{
    word addr2 = (addr + 1) & 0xFFFF;
    
    word res1 = mch_memory_fetch_decode_1(mb, addr);
    word res2 = mch_memory_fetch_decode_1(mb, addr2);
    
    var res = res1;
    res += (res2) << 8;
    
    return res;
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("O1"))) static word mch_resolve_mic_execute_deref_2_aligned(self mb, word addr)
{
    // This may have to be volatile, otherwise
    //  an LDRH is emitted, which is no cool, as
    //  the pointer will very likely be not aligned.
    // This sadly wastes precious CPU cycles,
    //  but it's necessary to avoid unaligned data abort.
    const r8* __restrict ptr = mch_resolve_mic_execute(mb, addr);
    
    word nres = ptr[0];
    nres += ptr[1] << 8;
    
    return nres;
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static word mch_memory_fetch_decode_2(self mb, word addr)
{
#if !CONFIG_MIC_CACHE_BYPASS
    
    if(COMPILER_LIKELY(addr < 0xFE00)) // yeah, this is an off-by-one error, and I don't care
    {
        //word r1 = MICACHE_R_VALUE(addr);
        //word r2 = MICACHE_R_VALUE(addr + 1);
        //if(r1 == r2)
        
        if(COMPILER_LIKELY((addr + 1) & MICACHE_R_SEL)) // same as above commented code
        {
            return mch_resolve_mic_execute_deref_2_aligned(mb, addr);
        }
    }
#endif
    
    return mch_memory_fetch_decode_2_slow(mb, addr);
}

PGB_FUNC ATTR_HOT word mch_memory_fetch_PC_op_2(self mb)
{
    word addr = mb->PC;
    mb->PC = (addr + 2) & 0xFFFF;
    
    word resp = mch_memory_fetch_decode_2(mb, addr);
    DBGF("- /O2 %04X <> %04X\n", addr, resp);
    return resp;
}


#pragma endregion

#pragma region Dispatch read and write

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) void mch_memory_dispatch_write(self mb, word addr, word data)
{
    DBGF("- /WR %04X <- %02X\n", addr, data);
    
    if(COMPILER_LIKELY(addr >= 0x8000))
    {
        if(COMPILER_LIKELY(addr < 0xFE00))
            mch_resolve_mic_write_deref(mb, addr, data);
        else
            mch_memory_dispatch_write_fexx_ffxx(mb, addr, data);
    }
    else
        mch_memory_dispatch_write_ROM(mb, addr, data);
}

#if !CONFIG_DBG
#define mch_memory_dispatch_read_ mch_memory_dispatch_read
#endif

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) word mch_memory_dispatch_read_(self mb, word addr)
{
    if(COMPILER_LIKELY(addr < 0xFE00))
        return mch_resolve_mic_read_deref(mb, addr);
    else
        return mch_memory_dispatch_read_fexx_ffxx(mb, addr);
}

#if CONFIG_DBG
PGB_FUNC ATTR_HOT word mch_memory_dispatch_read(self mb, word addr)
{
    DBGF("- /RD %04X -> ", addr);
    word res = mch_memory_dispatch_read_(mb, addr);
    DBGF("%02X\n", res);
    return res;
}
#endif

#pragma endregion

#pragma endregion

