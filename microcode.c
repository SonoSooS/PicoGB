#include <stdio.h>

#include "microcode.h"
#include "dbg.h"


#define self mb_state* __restrict


#if GBA
#define IR_F_COL (IR & 7)
#define IR_F_ROW ((IR >> 3) & 7)
#else
#define IR_F_COL IR_column
#define IR_F_ROW IR_row
#endif

#define MB_AF_R ((mb->reg.A << 8) | mb->reg.F)
#define MB_AF_W(v) {mb->reg.A = ((v) >> 8) & 0xFF; mb->reg.F = (v) & MB_FLAG_BITS;}
#define MB_CC_CHECK (mbh_cc_check(IR, mb->reg.F))

#define USE_MIC struct mb_mi_cache* __restrict mic = &mb->micache;
#define USE_MI struct mi_dispatch* __restrict mi = mb->mi;


#pragma region Microcode I/O

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

// Uncached resolve aligned readable const memory area, based on address
PGB_FUNC ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static const r8* __restrict mch_resolve_mic_r_direct_read(const self mb, word r_addr)
{
    USE_MI;
    
    const r8* __restrict ret = NULL;
    
    if(r_addr < MICACHE_R_VALUE(0x8000))
    {
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
    else if(r_addr < MICACHE_R_VALUE(0xA000))
    {
        const r8* __restrict ptr = &mi->VRAM[mi->BANK_VRAM << 13];
        
        r_addr -= MICACHE_R_VALUE(0x8000);
        return &(ptr[r_addr << MICACHE_R_BITS]);
    }
    else if(r_addr < MICACHE_R_VALUE(0xC000))
    {
        const r8* __restrict ptr = &mi->SRAM[mi->BANK_SRAM << 13];
        
        r_addr -= MICACHE_R_VALUE(0xA000);
        return &(ptr[r_addr << MICACHE_R_BITS]);
    }
    else // WRAM only [$C000; $FFFF] for OAMDMA
    {
        if(!(r_addr & MICACHE_R_VALUE(0x1000)))
        {
            r_addr &= MICACHE_R_VALUE(0x0FFF);
            return &(mi->WRAM[r_addr << MICACHE_R_BITS]);
        }
        else
        {
            var bank = mi->BANK_WRAM;
            if(!bank)
                bank = 1;
            
            r_addr &= MICACHE_R_VALUE(0x0FFF);
            return &(mi->WRAM[(bank << 12) + (r_addr << MICACHE_R_BITS)]);
        }
    }
}

PGB_FUNC ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static r8* __restrict mch_resolve_mic_r_direct_write(const self mb, word r_addr)
{
    USE_MI;
    
    if(r_addr < MICACHE_R_VALUE(0x8000))
    {
        // ROM is *Read-Only* Memory, can't write to it normally
        return NULL;
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
        if(!(r_addr & MICACHE_R_VALUE(0x1000)))
        {
            r_addr &= MICACHE_R_VALUE(0x0FFF);
            return &(mi->WRAM[r_addr << MICACHE_R_BITS]);
        }
        else
        {
            var bank = mi->BANK_WRAM;
            if(!bank)
                bank = 1;
            
            r_addr &= MICACHE_R_VALUE(0x0FFF);
            return &(mi->WRAM[(bank << 12) + (r_addr << MICACHE_R_BITS)]);
        }
    }
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
        
        return &ptr[addr & MICACHE_R_SEL];
    }
    
    return NULL;
}

PGB_FUNC ATTR_HOT ATTR_FORCE_INLINE __attribute__((optimize("O2"))) static inline const r8* __restrict mch_resolve_mic_read(self mb, word addr)
{
    
#if !CONFIG_MIC_CACHE_BYPASS
    var r_addr = MICACHE_R_VALUE(addr);
    
    const r8* __restrict ptr;
    
    ptr = mb->micache.mc_read[r_addr];
    if(COMPILER_LIKELY(ptr != NULL))
        return &ptr[addr & MICACHE_R_SEL];
#endif
    
    return mch_resolve_mic_read_slow(mb, addr);
}

PGB_FUNC ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static word mch_resolve_mic_read_slow_deref(self mb, word addr)
{
    return *mch_resolve_mic_read_slow(mb, addr);
}

PGB_FUNC ATTR_HOT ATTR_FORCE_INLINE __attribute__((optimize("O2"))) static inline word mch_resolve_mic_read_deref(self mb, word addr)
{
#if !CONFIG_MIC_CACHE_BYPASS
    var r_addr = MICACHE_R_VALUE(addr);
    
    const r8* __restrict ptr;
    
    ptr = mb->micache.mc_read[r_addr];
    if(COMPILER_LIKELY(ptr != NULL))
        return ptr[addr & MICACHE_R_SEL];
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
        ptr[addr & MICACHE_R_SEL] = data;
        return;
    }
    
    *ptr = data;
}

PGB_FUNC ATTR_HOT __attribute__((optimize("Os"))) static void mch_resolve_write_deref(r8* __restrict ptr, word addr, word data)
{
    addr &= MICACHE_R_SEL;
    COMPILER_VARIABLE_BARRIER(addr);
    ptr[addr] = data;
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("O2"))) static void mch_resolve_mic_write_deref(self mb, word addr, word data)
{
#if !CONFIG_MIC_CACHE_BYPASS
    var r_addr = MICACHE_R_VALUE(addr);
    
    r8* __restrict ptr;
    
    ptr = mb->micache.mc_write[r_addr];
    COMPILER_VARIABLE_BARRIER(ptr);
    if(ptr != NULL)
    {
        mch_resolve_write_deref(ptr, addr, data); // prevent register allocator spill, as it's slower than a tail call
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
        
        return &ptr[addr & MICACHE_R_SEL];
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
        return &ptr[addr & MICACHE_R_SEL];
#endif
    
    return mch_resolve_mic_execute_slow(mb, addr);
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static const r8* __restrict mch_resolve_mic_execute_noinline(self mb, word addr)
{
    return mch_resolve_mic_execute(mb, addr);
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
        return ptr[addr & MICACHE_R_SEL];
    else
#endif
    
    return mch_resolve_mic_execute_slow_deref(mb, addr);
}

#pragma endregion

#pragma endregion

#pragma region Dispatch special (IO + ROM)

PGB_FUNC static word mch_memory_dispatch_read_fexx_ffxx(const self mb, word addr)
{
    if(addr >= 0xFF80) // HRAM + IE
    {
        var hm = addr & 0xFF;
        
        if(hm != 0xFF)
        {
            return mb->mi->HRAM[hm - 0x80];
        }
        else
        {
            return mb->IE;
        }
    }
    
    // Handle IO by fabric
    return mb->mi->dispatch_IO(mb->mi->userdata, addr, MB_DATA_DONTCARE, MB_TYPE_READ);
}

PGB_FUNC static void mch_memory_dispatch_write_fexx_ffxx(self mb, word addr, word data)
{
    if(addr >= 0xFF80) // HRAM + IE
    {
        var hm = addr & 0xFF;
        
        if(hm != 0xFF)
        {
            mb->mi->HRAM[hm - 0x80] = data;
        }
        else
        {
            mb->IE = data & 0xFF;
        }
        
        return;
    }
    
    // Handle IO by fabric
    mb->mi->dispatch_IO(mb->mi->userdata, addr, data, MB_TYPE_WRITE);
}

PGB_FUNC ATTR_FORCE_NOINLINE static void mch_memory_dispatch_write_ROM(const self mb, word addr, word data)
{
    // Write to ROM has special meaning, handle by fabric
    mb->mi->dispatch_ROM(mb->mi->userdata, addr, data, MB_TYPE_WRITE);
}

#pragma endregion

#pragma region Dispatch

// Handle read from memory by microcode, including ECHO RAM
PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static word mch_memory_dispatch_read_(self mb, word addr)
{
    if(COMPILER_LIKELY(addr < 0xFE00))
        return mch_resolve_mic_read_deref(mb, addr);
    else
        return mch_memory_dispatch_read_fexx_ffxx(mb, addr);
}

#if CONFIG_DBG
PGB_FUNC static word mch_memory_dispatch_read(self mb, word addr)
{
    DBGF("- /RD %04X -> ", addr);
    word res = mch_memory_dispatch_read_(mb, addr);
    DBGF("%02X\n", res);
    return res;
}
#else
#define mch_memory_dispatch_read mch_memory_dispatch_read_
#endif

// Handle write to address from microcode, including ECHO RAM support
PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static void mch_memory_dispatch_write(self mb, word addr, word data)
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

// Fetch one byte as part of an instruction
PGB_FUNC ATTR_HOT ATTR_FORCE_INLINE __attribute__((optimize("Os"))) static word mch_memory_fetch_decode_1(self mb, word addr)
{
    if(COMPILER_LIKELY(addr < 0xFE00))
        return mch_resolve_mic_execute_deref(mb, addr);
    else
        return mch_memory_dispatch_read_fexx_ffxx(mb, addr);
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE static word mch_memory_fetch_decode_1_noinline(self mb, word addr)
{
    return mch_memory_fetch_decode_1(mb, addr);
}

PGB_FUNC ATTR_FORCE_NOINLINE __attribute__((optimize("O2"))) static word mch_memory_fetch_decode_2_slow(self mb, word addr)
{
    word addr2 = (addr + 1) & 0xFFFF;
    
    word res1 = mch_memory_fetch_decode_1_noinline(mb, addr);
    word res2 = mch_memory_fetch_decode_1_noinline(mb, addr2);
    
    var res = res1;
    res += (res2) << 8;
    
    return res;
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("O2"))) static word mch_resolve_mic_execute_deref_2_HRAM(self mb, word addr)
{
    addr = (addr - 0x80) & 0xFF;
    
    const volatile r8* __restrict ptr = &mb->mi->HRAM[addr];
    
    var nres = ptr[0];
    nres += ptr[1] << 8;
    
    return nres;
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("O2"))) static word mch_resolve_mic_execute_deref_2(self mb, word addr)
{
    // This has to be volatile, otherwise
    //  an LDRH is emitted, which is no cool, as
    //  the pointer will very likely be not aligned.
    // This sadly wastes precious CPU cycles,
    //  but it's necessary to avoid unaligned data abort.
    const volatile r8* __restrict ptr = mch_resolve_mic_execute(mb, addr);
    
    word nres = ptr[0];
    nres += ptr[1] << 8;
    
    return nres;
}

// Fetch two bytes as part of an instruction
PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static word mch_memory_fetch_decode_2(self mb, word addr)
{
#if !CONFIG_MIC_CACHE_BYPASS
    
    if(COMPILER_LIKELY(addr < 0xFE00)) // yeah, this is an off-by-one error, and I don't care
    {
        //word r1 = MICACHE_R_VALUE(addr);
        //word r2 = MICACHE_R_VALUE(addr + 1);
        //if(COMPILER_LIKELY(r1 == r2))
        
        word tmp = ~addr;
        COMPILER_VARIABLE_BARRIER(tmp);
        tmp &= MICACHE_R_SEL;
        COMPILER_VARIABLE_BARRIER(tmp);
        
        if(COMPILER_LIKELY(tmp)) // same as above commented code
        {
            return mch_resolve_mic_execute_deref_2(mb, addr);
        }
    }
    else if(addr >= 0xFF80) // This is off by 2, but let's be honest, if this triggers, we're already lost
    {
        return mch_resolve_mic_execute_deref_2_HRAM(mb, addr);
    }
#endif
    
    return mch_memory_fetch_decode_2_slow(mb, addr);
}

// Fetch one byte from PC, incrementing it as well
PGB_FUNC ATTR_HOT static word mch_memory_fetch_PC(self mb)
{
    word addr = mb->PC;
    mb->PC = (addr + 1) & 0xFFFF;
    
    var res = mch_memory_fetch_decode_1(mb, addr);
    DBGF("- /M1 %04X <> %02X\n", addr, res);
    return res;
}

PGB_FUNC ATTR_HOT static word mch_memory_fetch_PC_op_1(self mb)
{
    word addr = mb->PC;
    mb->PC = (addr + 1) & 0xFFFF;
    
    var res = mch_memory_fetch_decode_1(mb, addr);
    DBGF("- /O1 %04X <> %02X\n", addr, res);
    return res;
}

// Fetch two bytes from PC, incrementing it both times as well
PGB_FUNC ATTR_HOT static word mch_memory_fetch_PC_op_2(self mb)
{
    word addr = mb->PC;
    mb->PC = (addr + 2) & 0xFFFF;
    
    word resp = mch_memory_fetch_decode_2(mb, addr);
    DBGF("- /O2 %04X <> %04X\n", addr, resp);
    return resp;
}

#pragma endregion

#pragma endregion

#pragma region Flag mode control

PGB_FUNC static inline void mbh_fr_set_r8_add(self mb, word left, word right)
{
    mb->FR1 = left;
    mb->FR2 = right;
    mb->FMC_MODE = MB_FMC_MODE_ADD_r8;
}

PGB_FUNC static inline void mbh_fr_set_r8_adc(self mb, word left, word right)
{
    mb->FR1 = left;
    mb->FR2 = right;
    mb->FMC_MODE = MB_FMC_MODE_ADC_r8;
}

PGB_FUNC static inline void mbh_fr_set_r8_sub(self mb, word left, word right)
{
    mb->FR1 = left;
    mb->FR2 = right;
    mb->FMC_MODE = MB_FMC_MODE_SUB_r8;
}

PGB_FUNC static inline void mbh_fr_set_r8_sbc(self mb, word left, word right)
{
    mb->FR1 = left;
    mb->FR2 = right;
    mb->FMC_MODE = MB_FMC_MODE_SBC_r8;
}

PGB_FUNC static inline void mbh_fr_set_r16_add(self mb, word left, word right)
{
    mb->FR1 = left;
    mb->FR2 = right;
    mb->FMC_MODE = MB_FMC_MODE_ADD_r16;
}

PGB_FUNC static inline void mbh_fr_set_r16_add_r8(self mb, word left, word right)
{
    mb->FR1 = left;
    mb->FR2 = right;
    mb->FMC_MODE = MB_FMC_MODE_ADD_r16_r8;
}

PGB_FUNC word mbh_fr_get(self mb, word Fin)
{
    if(mb->FMC_MODE == MB_FMC_MODE_NONE)
        return Fin;
    
    var n1 = mb->FR1;
    var n2 = mb->FR2;
    
    Fin &= ~MB_FLAG_H; //TODO: why clear HC here? explain.
    
    switch(mb->FMC_MODE & 0xF)
    {
        default:
            return Fin;
        
        case MB_FMC_MODE_ADD_r16_r8:
        {
            //if(((n1 & 0xF00) + (n2 & 0xF00)) > 0xFFF)
            if(((n1 & 0xF) + (n2 & 0xF)) > 0xF) // ???
                Fin |= MB_FLAG_H;
            
            break;
        }
        
        case MB_FMC_MODE_ADD_r16:
        {
            if(((n1 & 0xFFF) + (n2 & 0xFFF)) > 0xFFF)
                Fin |= MB_FLAG_H;
            
            break;
        }
        
        
        case MB_FMC_MODE_ADD_r8:
        {
            if(((n1 & 0xF) + (n2 & 0xF)) > 0xF)
                Fin |= MB_FLAG_H;
            
            break;
        }
        
        case MB_FMC_MODE_ADC_r8:
        {
            if(((n1 & 0xF) + (n2 & 0xF) + 1) > 0xF)
                Fin |= MB_FLAG_H;
            
            break;
        }
        
        case MB_FMC_MODE_SUB_r8:
        {
            if((n1 & 0xF) < (n2 & 0xF))
                Fin |= MB_FLAG_H;
            
            break;
        }
        
        case MB_FMC_MODE_SBC_r8:
        {
            if((n1 & 0xF) < ((n2 & 0xF) + 1))
                Fin |= MB_FLAG_H;
            
            break;
        }
    }
    
    mb->FMC_MODE = MB_FMC_MODE_NONE;
    return Fin;
}

PGB_FUNC static inline wbool mbh_cc_check_0(word F)
{
    return ~F & MB_FLAG_Z; // NZ
}

PGB_FUNC static inline wbool mbh_cc_check_1(word F)
{
    return F & MB_FLAG_Z; // Z
}

PGB_FUNC static inline wbool mbh_cc_check_2(word F)
{
    return ~F & MB_FLAG_C; // NC
}

PGB_FUNC static inline wbool mbh_cc_check_3(word F)
{
    return F & MB_FLAG_C; // C
}

PGB_FUNC static wbool mbh_cc_check(word IR, word F)
{
    register word IR_r = (IR >> 3) & 3;
    
    if(IR_r == MB_CC_NZ)
        return mbh_cc_check_0(F); // NZ
    else if(IR_r == MB_CC_Z)
        return mbh_cc_check_1(F); // Z
    else if(IR_r == MB_CC_NC)
        return mbh_cc_check_2(F); // NC
    else if(IR_r == MB_CC_C)
        return mbh_cc_check_3(F); // C
    
    __builtin_unreachable();
}

#pragma endregion

#pragma region disasm (unfinished)
#if CONFIG_DBG

static const char* str_r8[8] = {"B", "C", "D", "E", "H", "L", "[HL]", "A"};
static const char* str_aluop[8] = {"ADD", "ADC", "SUB", "SBC", "AND", "XOR", "ORR", "CMP"};

void mb_disasm(const struct mb_state* __restrict mb)
{
    var IR = mb->IR.low;
    
    switch(IR >> 6)
    {
        case 1:
            printf("LD %s, %s", str_r8[(IR >> 3) & 7], str_r8[IR & 7]);
            break;
        
        case 2:
            printf("%s A, %s", str_aluop[(IR >> 3) & 7], str_r8[IR & 7]);
            break;
    }
    
    puts("");
}

static const char* str_cbop[4] = {0, "BIT", "SET", "RES"};
static const char* str_cbop0[8] = {"ROL", "ROR", "RCL", "RCR", "LSL", "ASR", "SWAP", "LSR"};

void mb_disasm_CB(const struct mb_state* __restrict mb, word CBIR)
{
    var IR = CBIR;
    
    if(IR >> 6)
    {
        printf("%s %s.%u", str_cbop[(IR >> 6) & 3], str_r8[IR & 7], (IR >> 3) & 7);
    }
    else
    {
        printf("%s %s", str_cbop0[(IR >> 3) & 7], str_r8[IR & 7]);
    }
    
    puts("");
}

#endif
#pragma endregion

PGB_FUNC ATTR_HOT word mb_exec(self mb)
{
    register var IR = mb->IR.low;
    r16* __restrict p_reg16_ptr;
    
    // Instruction column left to right
    var IR_column = IR & 7;
    // Instruction row top to bottom
    var IR_row = (IR >> 3) & 7;
    
    // Cycle count
    var ncycles = 0;
    
    // Index of source or read-only register
    var i_src;
    // Index of destination or read-modify-write register index
    var i_dst;
    
    // Data for 8bit registers
    var data_reg;
    // Data for 16bit registers
    var data_wide;
    // Contains flags where necessary
    var data_flags;
    // Contains result data to be written back eventually
    var data_result;
    
    if(mb->IME) // Interrupts are enabled
    {
        var F = mbh_irq_get_pending(mb);
        if(F) // Handle IREQ if there is any
        {
            ++ncycles; // IDU decrement PC penalty cycle
            
            data_wide = (mb->PC - 1) & 0xFFFF;
            
            var i = 0;
            for(;;)
            {
                if(F & (1 << i))
                {
                    mb->PC = 0x40 + (i << 3);
                    break;
                }
                
                ++i;
            }
            
            mb->IME = 0;
            mb->IME_ASK = 0;
            
            mb->IF &= ~(1 << i);
        
        #if CONFIG_DBG
            DBGF("IRQ #%u\n", i);
        #endif
            
            goto generic_push;
        }
    }
    else if(mb->IME_ASK) // IME was asked to be turned on
    {
        mb->IME = 1;
        mb->IME_ASK = 0;
    }
    
#if CONFIG_DBG
    if(_IS_DBG)
    {
        DBGF("Instruction %02X (%01o:%01o:%01o) ", IR, IR >> 6, IR & 7, (IR >> 3) & 7);
        mb_disasm(mb);
    }
#endif
    
    #if 1
    if(IR >= 0xC0)
    {
        if(IR == 0xF0)
        {
            data_wide = 0xFF00 + mch_memory_fetch_PC_op_1(mb);
            goto instr_360;
        }
        else if(IR == 0xFA)
        {
            data_wide = mch_memory_fetch_PC_op_2(mb);
            goto instr_372;
        }
        else
            goto IR_case_3;
    }
    else if(IR >= 0x80)
        goto IR_case_2;
    else if(IR < 0x40)
    {
        if(IR_F_COL == 0)
            goto instr_0x0;
        else
            goto IR_case_0;
    }
    else
        goto IR_case_1;
    #endif
    
    switch((IR >> 6) & 3)
    {
        IR_case_0:
        case 0: // Top bullshit
            switch(IR_column)
            {
                instr_0x0:
                case 0: // whatever
                    switch(IR_row)
                    {
                        instr_00:
                        case 0: // NOP
                            goto generic_fetch;
                        instr_10:
                        case 2: // STOP
                            goto generic_fetch_stop; // STOP is just bugged NOP, lol
                        
                        instr_08:
                        case 1: // LD a16, SP
                        {
                            data_wide = mch_memory_fetch_PC_op_2(mb);
                            
                            var SP = mb->SP;
                            
                            mch_memory_dispatch_write(mb, data_wide + 0, SP & 0xFF);
                            mch_memory_dispatch_write(mb, (data_wide + 1) & 0xFFFF, SP >> 8);
                            
                            ncycles += 2 + 2; // a16 op + n16 write
                            goto generic_fetch;
                        }
                        
                        instr_18:
                        case 3: // JR e8
                        generic_jr:
                        {
                            var PC = mb->PC;
                            data_wide = mch_memory_fetch_PC_op_1(mb);
                            
                            // Wedge if unbreakable spinloop is detected
                            // TODO: unfuck this statement
                            if(data_wide == 0xFE && ((!mb->IME && !mb->IME_ASK) || (!mb->IE && !(mb->IF & 0x1F))))
                                return 0; // wedge until NMI
                            
                            if(data_wide >= 0x80)
                                data_wide += 0xFF00;
                            
                            mb->PC = (data_wide + PC + 1) & 0xFFFF;
                            
                            ncycles += 2; // op fetch + ALU IDU Cy magic
                            goto generic_fetch;
                        }
                        
                        case 4: if(mbh_cc_check_0(mb->reg.F)) goto generic_jr; else goto instr_JNR_cc_e8_fail;
                        case 5: if(mbh_cc_check_1(mb->reg.F)) goto generic_jr; else goto instr_JNR_cc_e8_fail;
                        case 6: if(mbh_cc_check_2(mb->reg.F)) goto generic_jr; else goto instr_JNR_cc_e8_fail;
                        case 7: if(mbh_cc_check_3(mb->reg.F)) goto generic_jr; else goto instr_JNR_cc_e8_fail;
                            
                            {
                            instr_JNR_cc_e8_fail:
                                mb->PC = (mb->PC + 1) & 0xFFFF;
                                ncycles += 1; // op fetch only
                                goto generic_fetch;
                            }
                        
                        default:
                            __builtin_unreachable();
                    }
                
                case 1: // random r16 bullshit
                {
                    i_dst = (IR >> 4) & 3;
                    
                    if(i_dst != 3)
                        p_reg16_ptr = &mb->reg.raw16[i_dst];
                    else
                        p_reg16_ptr = &mb->SP;
                    
                    if(!(IR & 8)) // LD r16, n16
                    {
                        instr_0x1_0:
                        data_wide = mch_memory_fetch_PC_op_2(mb);
                        
                        *p_reg16_ptr = data_wide;
                        
                        ncycles += 2; // n16 op fetch
                        goto generic_fetch;
                    }
                    else // ADD HL, r16
                    {
                        instr_0x1_1:
                        
                        data_reg = *p_reg16_ptr;
                        data_flags = mb->reg.F & MB_FLAG_Z;
                        
                        data_result = mb->reg.HL;
                        word mres = data_result + data_reg;
                        if(mres >> 16)
                            data_flags |= MB_FLAG_C;
                        
                        mb->reg.HL = mres;
                        mb->reg.F = data_flags;
                        
                        mbh_fr_set_r16_add(mb, data_result, data_reg);
                        
                        ncycles += 2 - 1; // 2nd ALU cycle parallel with fetch
                        goto generic_fetch;
                    }
                }
                
                case 2: // LD r16 ptr
                {
                    instr_0x2:
                    i_dst = (IR >> 4) & 3;
                    
                    if(i_dst < 2)
                        p_reg16_ptr = &mb->reg.raw16[i_dst];
                    else
                        p_reg16_ptr = &mb->reg.HL;
                    
                    if(IR & 8) // load ptr
                    {
                        mb->reg.A = mch_memory_dispatch_read(mb, *p_reg16_ptr);
                    }
                    else // store ptr
                    {
                        mch_memory_dispatch_write(mb, *p_reg16_ptr, mb->reg.A);
                    }
                    
                    if(i_dst < 2)
                        ;
                    else
                    {
                        if(!(i_dst & 1))
                            ++*p_reg16_ptr;
                        else
                            --*p_reg16_ptr;
                    }
                    
                    ncycles += 1; // memory op
                    goto generic_fetch;
                }
                
                case 3: // INCDEC r16
                {
                    instr_0x3:
                    i_dst = (IR >> 4) & 3;
                    
                    if(i_dst != 3)
                        p_reg16_ptr = &mb->reg.raw16[i_dst];
                    else
                        p_reg16_ptr = &mb->SP;
                    
                    if(!(IR & 8)) // INC r16
                    {
                        ++*p_reg16_ptr;
                    }
                    else // DEC r16
                    {
                        --*p_reg16_ptr;
                    }
                    
                    ncycles += 1; // IDU post-incdec
                    goto generic_fetch;
                }
                
                case 4: // INC r8
                case 5: // DEC r8
                {
                    instr_0x45:
                    // Z1H-
                    
                    i_dst = IR_F_ROW ^ 1;
                    
                    data_flags = mb->reg.F & MB_FLAG_C;
                    
                    if(i_dst != 7)
                        data_reg = mb->reg.raw8[i_dst];
                    else
                    {
                        ++ncycles; // memory access
                        data_reg = mch_memory_dispatch_read(mb, mb->reg.HL);
                    }
                    
                    if(IR & 1) // DEC
                    {
                        data_flags |= MB_FLAG_N;
                        
                        if((data_reg & 0xF) == 0)
                            data_flags |= MB_FLAG_H;
                        
                        data_reg = (data_reg - 1) & 0xFF;
                    }
                    else // INC
                    {
                        data_reg = (data_reg + 1) & 0xFF;
                        
                        if((data_reg & 0xF) == 0)
                            data_flags |= MB_FLAG_H;
                    }
                    
                    if(!data_reg)
                        data_flags |= MB_FLAG_Z;
                    
                    mb->reg.F = data_flags;
                    mb->FMC_MODE = MB_FMC_MODE_NONE; // we calculate flags in-place
                    
                    if(i_dst != 7)
                        mb->reg.raw8[i_dst] = data_reg;
                    else
                    {
                        ++ncycles; // memory access
                        mch_memory_dispatch_write(mb, mb->reg.HL, data_reg);
                    }
                    
                    goto generic_fetch;
                }
                
                case 6: // LD r8, n8
                {
                    instr_0x6:
                    data_reg = mch_memory_fetch_PC_op_1(mb);
                    i_dst = IR_F_ROW ^ 1;
                    ncycles += 1; // n8 operand fetch
                    goto generic_r8_write;
                }
                
                case 7: // generic bullshit
                    // just reimpl $CB func here, too much hassle to use goto
                    
                    switch(IR_row) 
                    {
                        instr_007:
                        case 0: // RLCA
                            data_reg = mb->reg.A;
                            data_flags = mb->reg.F;
                            data_flags &= MB_FLAG_C;
                            data_reg = (data_reg << 1) | (data_reg >> 7);
                            data_flags = (data_reg >> 4) & 0x10; // Cy flag
                            mb->reg.A = data_reg & 0xFF;
                            mb->reg.F = data_flags;
                            
                            mb->FMC_MODE = MB_FMC_MODE_NONE;
                            goto generic_fetch;
                        
                        instr_017:
                        case 1: // RRCA
                            data_reg = mb->reg.A;
                            data_flags = mb->reg.F;
                            data_flags &= MB_FLAG_C;
                            data_reg = (data_reg << 7) | (data_reg >> 1);
                            data_flags = (data_reg >> 3) & 0x10; // Cy flag
                            mb->reg.A = data_reg & 0xFF;
                            mb->reg.F = data_flags;
                            
                            mb->FMC_MODE = MB_FMC_MODE_NONE;
                            goto generic_fetch;
                        
                        instr_027:
                        case 2: // RLA
                            data_reg = mb->reg.A;
                            data_flags = mb->reg.F;
                            data_flags &= MB_FLAG_C;
                            data_reg = (data_reg << 1) | ((data_flags >> 4) & 1);
                            data_flags = (data_reg >> 4) & 0x10; // Cy flag
                            mb->reg.A = data_reg & 0xFF;
                            mb->reg.F = data_flags;
                            
                            mb->FMC_MODE = MB_FMC_MODE_NONE;
                            goto generic_fetch;
                        
                        instr_037:
                        case 3: // RRA
                            data_reg = mb->reg.A;
                            data_flags = mb->reg.F;
                            data_flags &= MB_FLAG_C;
                            data_reg = (data_reg << 8) | (data_reg >> 1) | ((data_flags & 0x10) << 3);
                            data_flags = (data_reg >> 4) & 0x10; // Cy flag
                            mb->reg.A = data_reg & 0xFF;
                            mb->reg.F = data_flags;
                            
                            mb->FMC_MODE = MB_FMC_MODE_NONE;
                            goto generic_fetch;
                        
                        instr_047:
                        case 4: // fuck DAA
                            data_reg = mb->reg.A;
                            data_flags = mb->reg.F;
                            data_flags &= ~MB_FLAG_Z;
                            
                            data_flags = mbh_fr_get(mb, data_flags);
                            
                            if(!(data_flags & MB_FLAG_N))
                            {
                                if((data_flags & MB_FLAG_H) || ((data_reg & 0xF) > 9))
                                {
                                    data_reg += 6;
                                    data_flags |= MB_FLAG_H;
                                }
                                
                                if((data_flags & MB_FLAG_C) || (((data_reg >> 4) & 0x1F) > 9))
                                {
                                    data_reg += 6 << 4;
                                    data_flags |= MB_FLAG_C;
                                }
                            }
                            else
                            {
                                // why the assymmetry???
                                
                                if((data_flags & MB_FLAG_H))// || ((wdat & 0xF) > 9))
                                {
                                    data_reg -= 6;
                                    data_flags |= MB_FLAG_H;
                                }
                                
                                if((data_flags & MB_FLAG_C))// || (((wdat >> 4) & 0x1F) > 9))
                                {
                                    data_reg -= 6 << 4;
                                    data_flags |= MB_FLAG_C;
                                }
                            }
                            
                            data_flags &= (MB_FLAG_C | MB_FLAG_N);
                            
                            data_reg &= 0xFF;
                            if(!data_reg)
                                data_flags |= MB_FLAG_Z;
                            mb->reg.A = data_reg;
                            mb->reg.F = data_flags;
                            
                            goto generic_fetch;
                        
                        instr_057:
                        case 5: // CPL A
                            mb->reg.F |= MB_FLAG_N | MB_FLAG_H;
                            mb->reg.A = ~mb->reg.A;
                            
                            mb->FMC_MODE = MB_FMC_MODE_NONE;
                            goto generic_fetch;
                        
                        instr_067:
                        case 6: // SET Cy
                            mb->reg.F = (mb->reg.F & MB_FLAG_Z) | MB_FLAG_C;
                            
                            mb->FMC_MODE = MB_FMC_MODE_NONE;
                            goto generic_fetch;
                        
                        instr_077:
                        case 7: // CPL Cy
                            mb->reg.F = (mb->reg.F ^ MB_FLAG_C) & (MB_FLAG_C | MB_FLAG_Z);
                            
                            mb->FMC_MODE = MB_FMC_MODE_NONE;
                            goto generic_fetch;
                        
                        default:
                            __builtin_unreachable();
                    }
            }
            return 0;
        
        IR_case_1:
        case 1: // MOV
            if(COMPILER_LIKELY(IR != 0x76))
            {
                instr_MOV:
                {
                #if GBA
                    var vIR = IR ^ 9;
                    isrc = vIR & 7;
                    idst = (vIR >> 3) & 7;
                #else
                    i_src = IR_column ^ 1;
                    i_dst = IR_row ^ 1;
                #endif
                }
                
                if(i_src != 7)
                {
                    data_reg = mb->reg.raw8[i_src];
                }
                else
                {
                    ++ncycles; // memory access
                    data_reg = mch_memory_dispatch_read(mb, mb->reg.HL);
                }
                
            generic_r8_write:
                if(i_dst != 7)
                {
                    mb->reg.raw8[i_dst] = data_reg;
                }
                else
                {
                    ++ncycles; // memory access
                    mch_memory_dispatch_write(mb, mb->reg.HL, data_reg);
                }
                
                goto generic_fetch;
            }
            else
            {
                instr_0x76:
                goto generic_fetch_halt;
            }
        
        IR_case_2:
        case 2: // ALU r8
        {
            // A is always the src and dst, thank fuck
            
            instr_ALU:
            
            i_src = IR_F_COL ^ 1;
            
            if(i_src != 7)
            {
                data_reg = mb->reg.raw8[i_src];
            }
            else
            {
                ++ncycles; // memory access
                data_reg = mch_memory_dispatch_read(mb, mb->reg.HL);
            }
            
            alu_op_begin:
            data_result = mb->reg.A;
            
            switch(IR_F_ROW)
            {
                instr_ALU_0:
                case 0: // ADD Z0HC
                    mbh_fr_set_r8_add(mb, data_result, data_reg);
                    
                instr_ALU_0_cont:
                    data_result = data_result + data_reg;
                    if(data_result >> 8)
                        data_flags = MB_FLAG_C;
                    else
                        data_flags = 0;
                    
                    
                    break;
                
                //instr_ALU_1:
                case 1: // ADC Z0HC
                    if(mb->reg.F & 0x10)
                    {
                        mbh_fr_set_r8_adc(mb, data_result, data_reg);
                        data_reg += 1;
                        goto instr_ALU_0_cont;
                    }
                    else
                    {
                        goto instr_ALU_0;
                    }
                
                instr_ALU_2:
                case 2: // SUB Z1HC
                    mbh_fr_set_r8_sub(mb, data_result, data_reg);
                    
                instr_ALU_2_cont:
                    data_result = data_result - data_reg;
                    if(data_result >> 8)
                        data_flags = MB_FLAG_N | MB_FLAG_C;
                    else
                        data_flags = MB_FLAG_N;
                    
                    break;
                
                //instr_ALU_3:
                case 3: // SBC Z1HC
                    if(mb->reg.F & MB_FLAG_C)
                    {
                        mbh_fr_set_r8_sbc(mb, data_result, data_reg);
                        data_reg += 1;
                        goto instr_ALU_2_cont;
                    }
                    else
                    {
                        goto instr_ALU_2;
                    }
                
                 //instr_ALU_7:
                case 7: // CMP Z1HC
                    mbh_fr_set_r8_sub(mb, data_result, data_reg);
                    
                    data_result = data_result - data_reg;
                    if(data_result >> 8)
                        data_flags = MB_FLAG_N | MB_FLAG_C;
                    else
                        data_flags = MB_FLAG_N;
                    
                    if(!(data_result & 0xFF))
                        data_flags |= MB_FLAG_Z;
                    
                    mb->reg.F = data_flags;
                    
                    goto generic_fetch;
                
                //instr_ALU_4:
                case 4: // AND Z010
                    mb->FMC_MODE = MB_FMC_MODE_NONE;
                    
                    data_flags = MB_FLAG_H;
                    data_result &= data_reg;
                    break;
                
                //instr_ALU_5:
                case 5: // XOR Z000
                    mb->FMC_MODE = MB_FMC_MODE_NONE;
                    
                    data_flags = 0;
                    data_result ^= data_reg;
                    break;
                
                //instr_ALU_6:
                case 6: // ORR Z000
                    mb->FMC_MODE = MB_FMC_MODE_NONE;
                    
                    data_flags = 0;
                    data_result |= data_reg;
                    break;
                
                default:
                    __builtin_unreachable();
            }
            
            if(!(data_result & 0xFF))
                data_flags |= MB_FLAG_Z;
            
            {
                hilow16_t sta;
                sta.low = data_result;
                sta.high = data_flags;
                mb->reg.hilo16[3] = sta; // REG_FA (yes, not AF)
            }
            
            goto generic_fetch;
        }
        
        IR_case_3:
        case 3: // Bottom bullshit
            switch(IR_column)
            {
                case 0: // misc junk and RET cc
                {
                    if(IR & 0x20) // misc junk (bottom 4)
                    {
                        if(!(IR & 8)) // LDH a8
                        {
                            data_wide = 0xFF00 + mch_memory_fetch_PC_op_1(mb);
                            
                            if(IR & 0x10)
                            {
                                instr_360:
                                DBGF("- /HR %04X -> ", data_wide);
                                mb->reg.A = mch_memory_dispatch_read_fexx_ffxx(mb, data_wide);
                                DBGF("%02X\n", mb->reg.A);
                            }
                            else
                            {
                                instr_340:
                                DBGF("- /HW %04X <- %02X\n", data_wide, mb->reg.A);
                                mch_memory_dispatch_write_fexx_ffxx(mb, data_wide, mb->reg.A);
                            }
                            
                            ncycles += 2; // a8 op + memory access
                            goto generic_fetch;
                        }
                        else
                        {
                        instr_weird_r16_r8:
                            data_wide = mch_memory_fetch_PC_op_1(mb);
                            if(data_wide >= 0x80)
                                data_wide += 0xFF00;
                            
                            data_reg = mb->SP;
                            //mbh_fr_set_r16_add_r8(mb, data_reg, data_wide);
                            mb->FMC_MODE = MB_FMC_MODE_NONE; // fuck this, the call rate is so low that it's cheaper to do this in-place
                            
                            data_flags = 0;
                            
                            if(((data_wide & 0xFF) + (data_reg & 0xFF)) >> 8)
                                data_flags |= MB_FLAG_C;
                            
                            if(((data_wide & 0xF) + (data_reg & 0xF)) >> 4)
                                data_flags |= MB_FLAG_H;
                            
                            data_wide = (data_wide + data_reg) & 0xFFFF;
                            
                            mb->reg.F = data_flags;
                            
                            if(IR & 0x10) // HL, SP+e8
                            {
                                mb->reg.HL = data_wide;
                                ncycles += 2; // operand + magic
                            }
                            else // SP, SP+e8
                            {
                                mb->SP = data_wide;
                                ncycles += 3; // operand + 2x ALU
                            }
                            
                            goto generic_fetch;
                        }
                    }
                    else // RET cc
                    {
                        instr_RET_cc:
                        
                        ncycles += 1; // cc_check penalty cycle
                        
                        if(!MB_CC_CHECK)
                        {
                            goto generic_fetch;
                        }
                        else
                        {
                            goto generic_ret;
                        }
                    }
                }
                
                case 1: // POP r16 and junk
                {
                    if(IR & 8) // junk
                    {
                        switch((IR >> 4) & 3)
                        {
                            instr_311:
                            case 0: // RET
                            generic_ret:
                            {
                                var SP = mb->SP;
                                data_wide = mch_memory_dispatch_read(mb, SP++);
                                data_wide |= mch_memory_dispatch_read(mb, (SP++) & 0xFFFF) << 8;
                                mb->SP = SP;
                                
                                mb->PC = data_wide;
                                
                                ncycles += 2 + 1; // 2x LD Imm.(L|H), [SP+] + MOV PC, Imm
                                goto generic_fetch;
                            }
                            
                            instr_331:
                            case 1: // IRET
                                mb->IME = 1;
                                mb->IME_ASK = 1;
                                goto generic_ret;
                            
                            instr_351:
                            case 2: // JP HL
                                mb->PC = mb->reg.HL;
                                goto generic_fetch; // no cycle penalty, IDU magic
                            
                            instr_371:
                            case 3: // MOV SP, HL
                                mb->SP = mb->reg.HL;
                                ncycles += 1; // cross-IDU wide bus move penalty
                                goto generic_fetch;
                            
                            default:
                                __builtin_unreachable();
                        }
                    }
                    else // POP r16
                    {
                        var SP;
                        instr_POP_r16:
                        
                        SP = mb->SP;
                        data_wide = mch_memory_dispatch_read(mb, SP++);
                        data_wide |= mch_memory_dispatch_read(mb, (SP++) & 0xFFFF) << 8;
                        mb->SP = SP;
                        
                        i_src = (IR >> 4) & 3;
                        if(i_src != 3)
                        {
                            mb->reg.raw16[i_src] = data_wide;
                        }
                        else
                        {
                            MB_AF_W(data_wide);
                            mb->FMC_MODE = MB_FMC_MODE_NONE; // overwritten manually from stack
                        }
                        
                        ncycles += 2; // 2x LD rD.(L|H), [SP+]
                        goto generic_fetch;
                    }
                }
                
                case 2: // LD mem or JP cc, a16
                {
                    if(IR & 0x20) // LD mem
                    {
                        if(IR & 8) // LD a16
                        {
                            data_wide = mch_memory_fetch_PC_op_2(mb);
                            
                            if(IR & 0x10)
                            {
                                instr_372:
                                mb->reg.A = mch_memory_dispatch_read(mb, data_wide);
                            }
                            else
                            {
                                instr_352:
                                mch_memory_dispatch_write(mb, data_wide, mb->reg.A);
                            }
                            
                            ncycles += 2 + 1; // a16 operand + memory access
                        }
                        else // LDH C
                        {
                            data_wide = 0xFF00 + mb->reg.C;
                            
                            if(!(IR & 0x10))
                            {
                                instr_342:
                                DBGF("- /HW %04X <- %02X\n", data_wide, mb->reg.A);
                                mch_memory_dispatch_write_fexx_ffxx(mb, data_wide, mb->reg.A);
                            }
                            else
                            {
                                instr_362:
                                DBGF("- /HR %04X -> ", data_wide);
                                mb->reg.A = mch_memory_dispatch_read_fexx_ffxx(mb, data_wide);
                                DBGF("%02X\n", mb->reg.A);
                            }
                            
                            ncycles += 1; // memory access
                        }
                        
                        goto generic_fetch;
                    }
                    else // JP cc, a16
                    {
                        instr_JP_cc_n16: //TODO: optimize this if possible
                        // small optimization, no imm fetch if no match
                        
                        if(MB_CC_CHECK)
                            goto generic_jp_abs;
                        else
                        {
                        instr_JP_cc_n16_fail:
                            mb->PC = (mb->PC + 2) & 0xFFFF;
                            ncycles += 2; // a16 fetch/"skip"
                            goto generic_fetch;
                        }
                    }
                }
                
                case 3: // junk
                {
                    switch(IR_F_ROW)
                    {
                        instr_303:
                        case 0: // JP a16
                        generic_jp_abs:
                        {
                            data_wide = mch_memory_fetch_PC_op_2(mb);
                            mb->PC = data_wide;
                            ncycles += 2 + 1; // a16 fetch + MOV PC, Imm through IDU
                            goto generic_fetch;
                        }
                        
                        instr_313:
                        case 1: // $CB
                            // Do not increment ncycles here,
                            //  it's handled in the $CB opcode handler
                            goto handle_cb;
                        
                        instr_363:
                        case 6: // DI
                            mb->IME = 0;
                            mb->IME_ASK = 0;
                            goto generic_fetch;
                        
                        instr_373:
                        case 7: // EI
                            mb->IME_ASK = 1;
                            goto generic_fetch;
                        
                        default:
                            return 0; //ILL
                    }
                }
                
                case 4: // CALL cc, a16
                {
                    // small optimization, do not fetch imm unless match
                    
                    if(!(IR & 32))
                    {
                        instr_CALL_cc_n16: //TODO: optimize
                        
                        if(MB_CC_CHECK)
                            goto generic_call;
                        else
                        {
                        instr_CALL_cc_n16_fail:
                            mb->PC = (mb->PC + 2) & 0xFFFF;
                            ncycles += 2; // a16 fetch/"skip"
                            goto generic_fetch;
                        }
                    }
                    else
                    {
                        return 0; //ILL
                    }
                }
                    
                case 5: // PUSH r16 / CALL a16
                {
                    if(!(IR & 8)) // PUSH r16
                    {
                        i_src = (IR >> 4) & 3;
                        if(i_src != 3)
                        {
                            data_wide = mb->reg.raw16[i_src];
                        }
                        else
                        {
                            mb->reg.F = mbh_fr_get(mb, mb->reg.F);
                            
                            data_wide = MB_AF_R;
                        }
                        
                    generic_push:
                        if(1)
                        {
                            var SP = mb->SP;
                            mch_memory_dispatch_write(mb, (--SP) & 0xFFFF, data_wide >> 8);
                            mch_memory_dispatch_write(mb, (--SP) & 0xFFFF, data_wide & 0xFF);
                            mb->SP = SP;
                            ncycles += 2 + 1; // SP- + [SP-] = rS.H + [SP] = rS.L
                        }
                        
                        goto generic_fetch;
                    }
                    else
                    {
                        if(IR_row == 1) // CALL a16
                        {
                        generic_call:
                        {
                            var PC = mb->PC;
                            data_wide = mch_memory_fetch_PC_op_2(mb);
                            mb->PC = data_wide;
                            data_wide = (PC + 2) & 0xFFFF;
                            ncycles += 2; // a16 fetch
                            
                            goto generic_push;
                        }
                        }
                        else
                        {
                            return 0; // ILL
                        }
                    }
                }
                
                case 6: // ALU n8
                {
                    instr_3x6:
                    ncycles += 1; // n8 operand fetch
                    data_reg = mch_memory_fetch_PC_op_1(mb);
                    goto alu_op_begin;
                }
                
                case 7: // RST
                {
                    instr_3x7:
                    data_wide = mb->PC;
                    mb->PC = IR & (7 << 3);
                    goto generic_push; // ncycle penalty applied at label
                }
            }
            return 0;
    }
    
    //goto generic_fetch;
    return 0; // WTF
    
    generic_fetch:
    mb->IR.raw = mch_memory_fetch_PC(mb);
    return ncycles + 1;
    
    generic_fetch_halt:
    {
        mb->IR.raw = mch_memory_fetch_PC(mb);
        
        mb->IMM.high = 0x76;
        
        mb->HALTING = !(mb->IE & mb->IF);
        if(mb->HALTING)
        {
            //mb->IR.raw = 0; // NOP
            return 1;
        }
    }
        
    generic_fetch_stop:
    {
        mb->IR.raw = mch_memory_fetch_PC(mb);
        
        mb->IMM.high = 0x10;
        
        mb->HALTING = 1;
        if(mb->HALTING)
        {
            mb->IR.raw = 0; // NOP
            return 1;
        }
    }
    
    handle_cb:
    if(1)
    {
        ncycles += 1; // $CB opcode fetch
        var CBIR = mch_memory_fetch_PC_op_1(mb);
        
    #if CONFIG_DBG
        if(_IS_DBG)
        {
            printf("               (%01o:%01o:%01o) ", CBIR >> 6, CBIR & 7, (CBIR >> 3) & 7);
            mb_disasm_CB(mb, CBIR);
        }
    #endif
    
        i_src = (CBIR >> 3) & 7;
        i_dst = (CBIR & 7) ^ 1;
        
        if(i_dst != 7)
            data_reg = mb->reg.raw8[i_dst];
        else
        {
            ++ncycles; // memory op fetch src
            data_reg = mch_memory_dispatch_read(mb, mb->reg.HL);
        }
        
        switch(CBIR >> 6)
        {
            case 0: // CB OP
                data_flags = mb->reg.F;
                data_flags &= MB_FLAG_C;
                
                switch(i_src)
                {
                    case 0: // RLC
                        data_reg = (data_reg << 1) | (data_reg >> 7);
                        data_flags = (data_reg >> 4) & 0x10; // Cy flag
                        break;
                    
                    case 1: // RRC
                        data_reg = (data_reg << 7) | (data_reg >> 1);
                        data_flags = (data_reg >> 3) & 0x10; // Cy flag
                        break;
                    
                    case 2: // RL
                        data_reg = (data_reg << 1) | ((data_flags >> 4) & 1);
                        data_flags = (data_reg >> 4) & 0x10; // Cy flag
                        break;
                    
                    case 3: // RR
                        data_reg = (data_reg << 8) | (data_reg >> 1) | ((data_flags & 0x10) << 3);
                        data_flags = (data_reg >> 4) & 0x10; // Cy flag
                        break;
                    
                    case 4: // LSL
                        data_reg = (data_reg << 1);
                        data_flags = (data_reg >> 4) & 0x10; // Cy flag
                        break;
                    
                    case 5: // ASR
                        data_flags = (data_reg & 1) << 4; // Cy flag
                        data_reg = (data_reg >> 1) | (data_reg & 0x80);
                        break;
                    
                    case 6: // SWAP
                        data_flags = 0;
                        data_reg = (data_reg >> 4) | (data_reg << 4);
                        break;
                    
                    case 7: // LSR
                        data_flags = (data_reg & 1) << 4; // Cy flag
                        data_reg = (data_reg >> 1);
                        break;
                }
                
                data_reg &= 0xFF;
                if(!data_reg)
                    data_flags |= MB_FLAG_Z;
                
                mb->reg.F = data_flags;
                goto cb_writeback;
            
            case 1: // BTT
                data_flags = mb->reg.F;
                
                if(data_reg & (1 << i_src))
                    data_flags = (data_flags & MB_FLAG_C) | (MB_FLAG_H);
                else
                    data_flags = (data_flags & MB_FLAG_C) | (MB_FLAG_H | MB_FLAG_Z);
                
                mb->reg.F = data_flags;
                
                goto generic_fetch;
            
            case 2: // RES
                data_reg &= ~(1 << i_src);
                goto cb_writeback;
            
            case 3: // SET
                data_reg |= (1 << i_src);
                goto cb_writeback;
        }
        
        return 0; // WTF
        
        cb_writeback:
        
        if(i_dst != 7)
            mb->reg.raw8[i_dst] = data_reg;
        else
        {
            ++ncycles; // memory op src writeback
            mch_memory_dispatch_write(mb, mb->reg.HL, data_reg);
        }
        
        goto generic_fetch;
    }
    return 0; // WTF
}
