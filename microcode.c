#include <stdio.h>

#include "microcode.h"
#include "dbg.h"


#define self mb_state* __restrict mb



#if GBA
#define F_COL (IR & 7)
#define F_ROW ((IR >> 3) & 7)
#else
#define F_COL column
#define F_ROW row
#endif

#define MB_R_AF ((mb->reg.A << 8) | mb->reg.F)
#define MB_W_AF(v) if(1){mb->reg.A = ((v) >> 8) & 0xFF; mb->reg.F = (v) & 0xF0;}
#define CC_CHECK (mbh_cc_check(IR, mb->reg.F))

#define USE_MIC struct mb_mi_cache* __restrict mic = &mb->micache;
#define USE_MI struct mi_dispatch* __restrict mi = mb->mi;


static const r8* mch_resolve_mic_read_internal(const self, word addr)
{
    //TODO: unfuck this entire thing
    
    const r8* ret = 0;
    
    USE_MI;
    
    var r_addr = MICACHE_R_VALUE(addr);
    if(r_addr < MICACHE_R_VALUE(0x8000))
    {
        if(r_addr < MICACHE_R_VALUE(0x4000))
        {
            return &(mi->ROM[0][addr & ~MICACHE_R_SEL]);
        }
        else
        {
            addr &= 0x3FFF;
            return &(mi->ROM[mi->BANK_ROM][addr & ~MICACHE_R_SEL]);
        }
    }
    else if(r_addr < MICACHE_R_VALUE(0xA000))
    {
        const r8* ptr = &mi->VRAM[mi->BANK_VRAM << 13];
        
        addr &= 0x1FFF;
        return &(ptr[addr & ~MICACHE_R_SEL]);
    }
    else if(r_addr < MICACHE_R_VALUE(0xC000))
    {
        addr &= 0x1FFF;
        
        const r8* ptr = &mi->SRAM[mi->BANK_SRAM << 13];
        
        return &(ptr[addr & ~MICACHE_R_SEL]);
    }
    else if(r_addr < MICACHE_R_VALUE(0xE000))
    {
        addr &= 0xFFF;
        
        if(r_addr < MICACHE_R_VALUE(0xD000))
        {
            return &(mi->WRAM[addr & ~MICACHE_R_SEL]);
        }
        else
        {
            var bank = mi->BANK_WRAM;
            if(!bank)
                bank = 1;
            
            return &(mi->WRAM[(bank << 12) + (addr & ~MICACHE_R_SEL)]);
        }
    }
    
    return 0;
}

static r8* mch_resolve_mic_write_internal(const self, word addr)
{
    //TODO: unfuck this entire thing
    
    r8* ret = 0;
    
    USE_MI;
    
    var r_addr = MICACHE_R_VALUE(addr);
    if(r_addr < MICACHE_R_VALUE(0x8000))
    {
        return 0;
    }
    else if(r_addr < MICACHE_R_VALUE(0xA000))
    {
        r8* ptr = &mi->VRAM[mi->BANK_VRAM << 13];
        
        addr &= 0x1FFF;
        return &(ptr[addr & ~MICACHE_R_SEL]);
    }
    else if(r_addr < MICACHE_R_VALUE(0xC000))
    {
        addr &= 0x1FFF;
        
        r8* ptr = &mi->SRAM[mi->BANK_SRAM << 13];
        
        return &(ptr[addr & ~MICACHE_R_SEL]);
    }
    else if(r_addr < MICACHE_R_VALUE(0xE000))
    {
        addr &= 0xFFF;
        
        if(r_addr < MICACHE_R_VALUE(0xD000))
        {
            return &(mi->WRAM[addr & ~MICACHE_R_SEL]);
        }
        else
        {
            var bank = mi->BANK_WRAM;
            if(!bank)
                bank = 1;
            
            return &(mi->WRAM[(bank << 12) + (addr & ~MICACHE_R_SEL)]);
        }
    }
    
    return 0;
}

static inline const r8* mch_resolve_mic_read(self, word addr)
{
    USE_MIC;
    
    var r_addr = MICACHE_R_VALUE(addr);
    
    const r8* ptr = mic->mc_read[r_addr];
    
    if(ptr)
        return &ptr[addr & MICACHE_R_SEL];
    
    ptr = mch_resolve_mic_read_internal(mb, addr);
    if(ptr)
    {
        mic->mc_read[r_addr] = ptr;
        return &ptr[addr & MICACHE_R_SEL];
    }
    
    return 0;
}

static inline const r8* mch_resolve_mic_execute(self, word addr)
{
    USE_MIC;
    
    var r_addr = MICACHE_R_VALUE(addr);
    
    const r8* ptr = mic->mc_execute[r_addr];
    
    if(ptr)
        return &ptr[addr & MICACHE_R_SEL];
    
    ptr = mch_resolve_mic_read_internal(mb, addr);
    if(ptr)
    {
        mic->mc_execute[r_addr] = ptr;
        return &ptr[addr & MICACHE_R_SEL];
    }
    
    return 0;
}

static inline r8* mch_resolve_mic_write(self, word addr)
{
    USE_MIC;
    
    var r_addr = MICACHE_R_VALUE(addr);
    
    r8* ptr = mic->mc_write[r_addr];
    
    if(ptr)
        return &ptr[addr & MICACHE_R_SEL];
    
    ptr = mch_resolve_mic_write_internal(mb, addr);
    if(ptr)
    {
        mic->mc_write[r_addr] = ptr;
        return &ptr[addr & MICACHE_R_SEL];
    }
    
    return 0;
}

static word mch_memory_dispatch_read_fexx_ffxx(const self, word addr)
{
    if(addr >= 0xFF80)
    {
        var hm = addr & 0xFF;
        if(hm != 0xFF)
        {
            USE_MI;
            
            return mi->HRAM[hm];
        }
        else
        {
            return mb->IE;
        }
    }
    
    return mb->mi->dispatch_IO(mb->mi->userdata, addr, 0, 0);
}

static void mch_memory_dispatch_write_fexx_ffxx(self, word addr, word data)
{
    if(addr >= 0xFF80)
    {
        var hm = addr & 0xFF;
        
        if(hm != 0xFF)
        {
            USE_MI;
            
            mi->HRAM[hm] = data;
        }
        else
        {
            mb->IE = data & 0xFF;
        }
        
        return;
    }
    
    mb->mi->dispatch_IO(mb->mi->userdata, addr, data, 1);
}

static inline void mch_memory_dispatch_write_ROM(const self, word addr, word data)
{
    mb->mi->dispatch_ROM(mb->mi->userdata, addr, data, 1);
}

static word mch_memory_dispatch_read_(self, word addr)
{
    USE_MIC;
    
    var r_addr = MICACHE_R_VALUE(addr);
    
    if(r_addr < MICACHE_R_VALUE(0xE000))
    {
        const r8* ptr;
    wram_read:
        ptr = mch_resolve_mic_read(mb, addr);
        return *ptr;
    }
    
    if(addr < 0xFE00)
    {
        addr -= 0x2000;
        r_addr = MICACHE_R_VALUE(addr);
        goto wram_read;
    }
    
    return mch_memory_dispatch_read_fexx_ffxx(mb, addr);
}

#if CONFIG_DBG
static word mch_memory_dispatch_read(self, word addr)
{
    DBGF("- /RD %04X -> ", addr);
    word res = mch_memory_dispatch_read_(mb, addr);
    DBGF("%02X\n", res);
    return res;
}
#else
#define mch_memory_dispatch_read mch_memory_dispatch_read_
#endif

static void mch_memory_dispatch_write(self, word addr, word data)
{
    DBGF("- /WR %04X <- %02X\n", addr, data);
    
    USE_MIC;
    
    var r_addr = MICACHE_R_VALUE(addr);
    
    if(r_addr >= MICACHE_R_VALUE(0x8000))
    {
        if(r_addr < MICACHE_R_VALUE(0xE000))
        {
            r8* ptr;
        wram_write:
            ptr = mch_resolve_mic_write(mb, addr);
            *ptr = data;
            return;
        }
        else if(addr < 0xFE00)
        {
            addr -= 0x2000;
            r_addr = MICACHE_R_VALUE(addr);
            goto wram_write;
        }
        
        /*return */mch_memory_dispatch_write_fexx_ffxx(mb, addr, data);
    }
    else
    {
        mch_memory_dispatch_write_ROM(mb, addr, data);
        micache_invalidate_range(mic, 0x4000, 0x7FFF);
        
        return;
    }
}

static inline word mch_memory_fetch_decode_1(self, word addr)
{
    USE_MIC;
    
    var r_addr = MICACHE_R_VALUE(addr);
    
    if(r_addr < MICACHE_R_VALUE(0xE000))
    {
        const r8* ptr;
    wram_read:
        ptr = mch_resolve_mic_execute(mb, addr);
        return *ptr;
    }
    
    if(addr < 0xFE00)
    {
        addr -= 0x2000;
        r_addr = MICACHE_R_VALUE(addr);
        goto wram_read;
    }
    
    return mch_memory_dispatch_read_fexx_ffxx(mb, addr);
}

static word mch_memory_fetch_decode_2(self, word addr)
{
    word addr2 = (addr + 1) & 0xFFFF;
    
    word r1 = MICACHE_R_VALUE(addr);
    word r2 = MICACHE_R_VALUE(addr2);
    if((r1 == r2) && (addr <= 0xDFFE))
    {
        USE_MIC;
        
        const r8* ptr = mch_resolve_mic_execute(mb, addr);
        
        var nres = *(ptr++);
        nres |= *(ptr++) << 8;
        
        return nres;
    }
    
    word res1 = mch_memory_fetch_decode_1(mb, addr);
    word res2 = mch_memory_fetch_decode_1(mb, addr2);
    
    var res = res1;
    res |= (res2) << 8;
    
    return res;
}

static word mch_memory_fetch_PC(self)
{
    word addr = mb->PC;
    mb->PC = (addr + 1) & 0xFFFF;
    
    var res = mch_memory_fetch_decode_1(mb, addr);
    DBGF("- /M1 %04X <> %02X\n", addr, res);
    return res;
}

static word mch_memory_fetch_PC_2(self)
{
    word addr = mb->PC;
    #if CONFIG_DBG
    word addr_orig = addr;
    #endif
    word resp = mch_memory_fetch_decode_2(mb, addr);
    mb->PC = (addr + 2) & 0xFFFF;
    
    DBGF("- /M2 %04X <> %04X\n", addr_orig, resp);
    return resp;
}

static inline word mbh_cc_check(word IR, word F)
{
    word res;
    
    switch((IR >> 3) & 3)
    {
        case 0: // NZ
            res = ~F & 0x80;
            return res;
        case 1: // Z
            res = F & 0x80;
            return res;
        case 2: // NC
            res = ~F & 0x10;
            return res;
        case 3: // C
            res = F & 0x10;
            return res;
        
        default:
            __builtin_unreachable();
    }
}

#pragma region disasm

static const char* str_r8[8] = {"B", "C", "D", "E", "H", "L", "[HL]", "A"};
static const char* str_aluop[8] = {"ADD", "ADC", "SUB", "SBC", "AND", "XOR", "ORR", "CMP"};

void disasm(const struct mb_state* __restrict mb)
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

void disasm_CB(const struct mb_state* __restrict mb, word CBIR)
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

#pragma endregion

word mb_exec(self)
{
    register var IR = mb->IR.low;
    r16* __restrict ld_dst_addr;
    
    var column = IR & 7;
    var row = (IR >> 3) & 7;
    
    var ncycles = 0;
    
    
    var rdat;
    var isrc, idst;
    var rres, rflag;
    var wdat;
    
    if(mb->IME && (mb->IE & mb->IF & 0x1F))
    {
        var F = (mb->IE & mb->IF & 0x1F);
        ++ncycles; // IDU decrement PC penalty cycle
        
        wdat = (mb->PC - 1) & 0xFFFF;
        
        var i = 0;
        for(;;)
        {
            if(F & (1 << i))
            {
                mb->PC = 0x40 | (i << 3);
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
    else if(mb->IME_ASK)
    {
        mb->IME = 1;
        mb->IME_ASK = 0;
    }
    
#if CONFIG_DBG
    if(_IS_DBG)
    {
        DBGF("Instruction %02X (%01o:%01o:%01o) ", IR, IR >> 6, IR & 7, (IR >> 3) & 7);
        disasm(mb);
    }
#endif
    
    #if 0
    switch(IR)
    {
        case 0x00: goto instr_00;
        case 0x10: goto instr_10;
        case 0x08: goto instr_08;
        case 0x18: goto instr_18;
        
        case 0x20:
        case 0x28:
        case 0x30:
        case 0x38:
            goto instr_JNR_cc_e8;
        
        case 0x01:
            ld_dst_addr = &mb->reg.raw16[0];
            goto instr_0x1_0;
        case 0x11:
            ld_dst_addr = &mb->reg.raw16[1];
            goto instr_0x1_0;
        case 0x21:
            ld_dst_addr = &mb->reg.raw16[2];
            goto instr_0x1_0;
        case 0x31:
            ld_dst_addr = &mb->SP;
            goto instr_0x1_0;
        
        case 0x09:
            ld_dst_addr = &mb->reg.raw16[0];
            goto instr_0x1_1;
        case 0x19:
            ld_dst_addr = &mb->reg.raw16[1];
            goto instr_0x1_1;
        case 0x29:
            ld_dst_addr = &mb->reg.raw16[2];
            goto instr_0x1_1;
        case 0x39:
            ld_dst_addr = &mb->SP;
            goto instr_0x1_1;
        
        case 0x02:
        case 0x12:
        case 0x22:
        case 0x32:
        case 0x0A:
        case 0x1A:
        case 0x2A:
        case 0x3A:
            goto instr_0x2;
        
        case 0x03:
        case 0x13:
        case 0x23:
        case 0x33:
        case 0x0B:
        case 0x1B:
        case 0x2B:
        case 0x3B:
            goto instr_0x3;
        
        case 0x04:
        case 0x14:
        case 0x24:
        case 0x34:
        case 0x05:
        case 0x15:
        case 0x25:
        case 0x35:
        case 0x0C:
        case 0x1C:
        case 0x2C:
        case 0x3C:
        case 0x0D:
        case 0x1D:
        case 0x2D:
        case 0x3D:
            goto instr_0x45;
        
        case 0x06:
        case 0x16:
        case 0x26:
        case 0x36:
        case 0x0E:
        case 0x1E:
        case 0x2E:
        case 0x3E:
            goto instr_0x6;
        
        case 0x07: goto instr_007;
        case 0x0F: goto instr_017;
        case 0x17: goto instr_027;
        case 0x1F: goto instr_037;
        case 0x27: goto instr_047;
        case 0x2F: goto instr_057;
        case 0x37: goto instr_067;
        case 0x3F: goto instr_077;
        
        case 0x76: goto instr_0x76;
        
        case 0x40:case 0x41:case 0x42:case 0x43:case 0x44:case 0x45:case 0x46:case 0x47:
        case 0x48:case 0x49:case 0x4A:case 0x4B:case 0x4C:case 0x4D:case 0x4E:case 0x4F:
        case 0x50:case 0x51:case 0x52:case 0x53:case 0x54:case 0x55:case 0x56:case 0x57:
        case 0x58:case 0x59:case 0x5A:case 0x5B:case 0x5C:case 0x5D:case 0x5E:case 0x5F:
        case 0x60:case 0x61:case 0x62:case 0x63:case 0x64:case 0x65:case 0x66:case 0x67:
        case 0x68:case 0x69:case 0x6A:case 0x6B:case 0x6C:case 0x6D:case 0x6E:case 0x6F:
        case 0x70:case 0x71:case 0x72:case 0x73:case 0x74:case 0x75:          case 0x77:
        case 0x78:case 0x79:case 0x7A:case 0x7B:case 0x7C:case 0x7D:case 0x7E:case 0x7F:
            goto instr_MOV;
        
        case 0x80:case 0x81:case 0x82:case 0x83:case 0x84:case 0x85:case 0x86:case 0x87:
        case 0x88:case 0x89:case 0x8A:case 0x8B:case 0x8C:case 0x8D:case 0x8E:case 0x8F:
        case 0x90:case 0x91:case 0x92:case 0x93:case 0x94:case 0x95:case 0x96:case 0x97:
        case 0x98:case 0x99:case 0x9A:case 0x9B:case 0x9C:case 0x9D:case 0x9E:case 0x9F:
        case 0xA0:case 0xA1:case 0xA2:case 0xA3:case 0xA4:case 0xA5:case 0xA6:case 0xA7:
        case 0xA8:case 0xA9:case 0xAA:case 0xAB:case 0xAC:case 0xAD:case 0xAE:case 0xAF:
        case 0xB0:case 0xB1:case 0xB2:case 0xB3:case 0xB4:case 0xB5:case 0xB6:case 0xB7:
        case 0xB8:case 0xB9:case 0xBA:case 0xBB:case 0xBC:case 0xBD:case 0xBE:case 0xBF:
            goto instr_ALU;
        
        
        case 0xC0:
        case 0xD0:
        case 0xC8:
        case 0xD8:
            goto instr_RET_cc;
        
        case 0xE0:
            wdat = 0xFF00 | mch_memory_fetch_PC(mb);
            goto instr_340;
        case 0xF0:
            wdat = 0xFF00 | mch_memory_fetch_PC(mb);
            goto instr_360;
        
        case 0xE8:
            wdat = mch_memory_fetch_PC(mb);
            if(wdat >= 0x80)
                wdat |= 0xFF00;
            
            wdat = (wdat + mb->SP) & 0xFFFF;
            goto instr_350;
        case 0xF8:
            wdat = mch_memory_fetch_PC(mb);
            if(wdat >= 0x80)
                wdat |= 0xFF00;
            
            wdat = (wdat + mb->SP) & 0xFFFF;
            goto instr_370;
        
        case 0xC9: goto instr_311;
        case 0xD9: goto instr_331;
        case 0xE9: goto instr_351;
        case 0xF9: goto instr_371;
        
        case 0xC1:
        case 0xD1:
        case 0xE1:
        case 0xF1:
            goto instr_POP_r16;
        
        case 0xEA:
            wdat = mch_memory_fetch_PC_2(mb);
            goto instr_352;
        case 0xFA:
            wdat = mch_memory_fetch_PC_2(mb);
            goto instr_372;
        
        case 0xE2:
            wdat = 0xFF00 | mb->reg.C;
            goto instr_342;
        case 0xF2:
            wdat = 0xFF00 | mb->reg.C;
            goto instr_362;
        
        case 0xC2:
        case 0xD2:
        case 0xCA:
        case 0xDA:
            goto instr_JP_cc_n16;
        
        case 0xC3: goto instr_303;
        case 0xCB: goto instr_313;
        case 0xF3: goto instr_363;
        case 0xFB: goto instr_373;
        
        case 0xC4:
        case 0xCC:
        case 0xD4:
        case 0xDC:
            goto instr_CALL_cc_n16;
        
        case 0xC5:
            wdat = mb->reg.raw16[0];
            goto generic_push;
        case 0xD5:
            wdat = mb->reg.raw16[1];
            goto generic_push;
        case 0xE5:
            wdat = mb->reg.raw16[2];
            goto generic_push;
        case 0xF5:
            wdat = MB_R_AF;
            goto generic_push;
        
        case 0xCD: goto generic_call;
        
        case 0xC6:
        case 0xD6:
        case 0xE6:
        case 0xF6:
        case 0xCE:
        case 0xDE:
        case 0xEE:
        case 0xFE:
            goto instr_3x6;
        
        case 0xC7:
        case 0xCF:
        case 0xD7:
        case 0xDF:
        case 0xE7:
        case 0xEF:
        case 0xF7:
        case 0xFF:
            goto instr_3x7;
        
        case 0xD3:
        case 0xDB:
        case 0xDD:
        case 0xE3:
        case 0xE4:
        case 0xEB:
        case 0xED:
        case 0xEC:
        case 0xF4:
        case 0xFC:
        case 0xFD:
            // HCF
            return 0;
        
        default:
            __builtin_unreachable();
    }
    #endif
    
    switch((IR >> 6) & 3)
    {
        case 0: // Top bullshit
            switch(column)
            {
                case 0: // whatever
                    switch(row)
                    {
                        instr_00:
                        case 0: // NOP
                        instr_10:
                        case 2: // STOP
                            goto generic_fetch; // STOP is just bugged NOP, lol
                        
                        instr_08:
                        case 1: // LD a16, SP
                        {
                            wdat = mch_memory_fetch_PC_2(mb);
                            
                            var SP = mb->SP;
                            
                            mch_memory_dispatch_write(mb, wdat + 0, SP & 0xFF);
                            mch_memory_dispatch_write(mb, (wdat + 1) & 0xFFFF, SP >> 8);
                            
                            ncycles += 2 + 2;
                            goto generic_fetch;
                        }
                        
                        instr_18:
                        case 3: // JR e8
                        generic_jr:
                        {
                            var PC = mb->PC;
                            wdat = mch_memory_fetch_PC(mb);
                            if(wdat == 0xFE && ((!mb->IME && !mb->IME_ASK) || (!mb->IE && !(mb->IF & 0x1F))))
                                return 0; // wedge until NMI
                            if(wdat >= 0x80)
                                wdat |= 0xFF00;
                            
                            mb->PC = (wdat + PC + 1) & 0xFFFF;
                            
                            ncycles += 2; // parallel add + fetch
                            goto generic_fetch;
                        }
                        
                        instr_JNR_cc_e8:
                        case 4: // JR cc, e8
                        case 5:
                        case 6:
                        case 7:
                            if(CC_CHECK)
                                goto generic_jr;
                            else
                            {
                                mb->PC = (mb->PC + 1) & 0xFFFF;
                                ncycles += 1;
                                goto generic_fetch;
                            }
                        
                        default:
                            __builtin_unreachable();
                    }
                
                case 1: // random r16 bullshit
                {
                    idst = (IR >> 4) & 3;
                    
                    if(idst != 3)
                        ld_dst_addr = &mb->reg.raw16[idst];
                    else
                        ld_dst_addr = &mb->SP;
                    
                    if(!(IR & 8)) // LD r16, n16
                    {
                        instr_0x1_0:
                        wdat = mch_memory_fetch_PC_2(mb);
                        
                        *ld_dst_addr = wdat;
                        
                        ncycles += 2;
                        goto generic_fetch;
                    }
                    else // ADD HL, r16
                    {
                        instr_0x1_1:
                        //TODO: half-carry bullshit, even though probably nothing uses flags from this instruction
                        
                        rdat = *ld_dst_addr;
                        rres = mb->reg.H;
                        rflag = mb->reg.F & 0x80;
                        
                        wdat = (rdat & 0xFF) + mb->reg.L;
                        if(wdat >> 8)
                            ++rres;
                        
                        if(((rres & 0xF) + ((rdat >> 8) & 0xF)) >> 4)
                            rflag |= 0x20;
                        
                        wdat = ((rres + (rdat >> 8)) << 8) | (wdat & 0xFF);
                        if(wdat >> 16)
                            rflag |= 0x10;
                        
                        
                        mb->reg.HL = wdat;
                        
                        mb->reg.F = rflag;
                        
                        ncycles += 2 - 1; // 2nd ALU cycle parallel with fetch
                        goto generic_fetch;
                    }
                }
                
                case 2: // LD r16 ptr
                {
                    instr_0x2:
                    idst = (IR >> 4) & 3;
                    
                    if(idst < 2)
                        ld_dst_addr = &mb->reg.raw16[idst];
                    else
                        ld_dst_addr = &mb->reg.HL;
                    
                    if(IR & 8) // load ptr
                    {
                        mb->reg.A = mch_memory_dispatch_read(mb, *ld_dst_addr);
                    }
                    else // store ptr
                    {
                        mch_memory_dispatch_write(mb, *ld_dst_addr, mb->reg.A);
                    }
                    
                    if(idst < 2)
                        ;
                    else
                    {
                        if(!(idst & 1))
                            ++*ld_dst_addr;
                        else
                            --*ld_dst_addr;
                    }
                    
                    ++ncycles;
                    goto generic_fetch;
                }
                
                case 3: // INCDEC r16
                {
                    instr_0x3:
                    idst = (IR >> 4) & 3;
                    
                    if(idst != 3)
                        ld_dst_addr = &mb->reg.raw16[idst];
                    else
                        ld_dst_addr = &mb->SP;
                    
                    if(!(IR & 8)) // INC r16
                    {
                        ++*ld_dst_addr;
                    }
                    else // DEC r16
                    {
                        --*ld_dst_addr;
                    }
                    
                    ++ncycles; // IDU busy penalty cycle
                    goto generic_fetch;
                }
                
                case 4: // INC r8
                case 5: // DEC r8
                {
                    instr_0x45:
                    // Z1H-
                    
                    idst = F_ROW ^ 1;
                    
                    rflag = mb->reg.F & 0x10;
                    
                    if(idst != 7)
                        rdat = mb->reg.raw8[idst];
                    else
                    {
                        rdat = mch_memory_dispatch_read(mb, mb->reg.HL);
                        ++ncycles;
                    }
                    
                    //TODO: fucking half carry flag
                    
                    if(IR & 1) // DEC
                    {
                        rflag |= 0x40;
                        
                        if((rdat & 0xF) == 0)
                            rflag |= 0x20;
                        
                        rdat = (rdat - 1) & 0xFF;
                    }
                    else // INC
                    {
                        rdat = (rdat + 1) & 0xFF;
                        
                        if((rdat & 0xF) == 0)
                            rflag |= 0x20;
                    }
                    
                    if(!rdat)
                        rflag |= 0x80;
                    
                    mb->reg.F = rflag;
                    
                    if(idst != 7)
                        mb->reg.raw8[idst] = rdat;
                    else
                    {
                        ++ncycles;
                        mch_memory_dispatch_write(mb, mb->reg.HL, rdat);
                    }
                    
                    goto generic_fetch;
                }
                
                case 6: // LD r8, n8
                {
                    instr_0x6:
                    rdat = mch_memory_fetch_PC(mb);
                    idst = F_ROW ^ 1;
                    ++ncycles;
                    goto generic_r8_write;
                }
                
                case 7: // generic bullshit
                    // just reimpl $CB func here, too much hassle to use goto
                    
                    switch(row) 
                    {
                        instr_007:
                        case 0: // RLCA
                            wdat = mb->reg.A;
                            rdat = mb->reg.F;
                            rdat &= 0x10;
                            wdat = (wdat << 1) | (wdat >> 7);
                            rdat = (wdat >> 4) & 0x10;
                            mb->reg.A = wdat & 0xFF;
                            mb->reg.F = rdat;
                            goto generic_fetch;
                        
                        instr_017:
                        case 1: // RRCA
                            wdat = mb->reg.A;
                            rdat = mb->reg.F;
                            rdat &= 0x10;
                            wdat = (wdat << 7) | (wdat >> 1);
                            rdat = (wdat >> 3) & 0x10;
                            mb->reg.A = wdat & 0xFF;
                            mb->reg.F = rdat;
                            goto generic_fetch;
                        
                        instr_027:
                        case 2: // RLA
                            wdat = mb->reg.A;
                            rdat = mb->reg.F;
                            rdat &= 0x10;
                            wdat = (wdat << 1) | ((rdat >> 4) & 1);
                            rdat = (wdat >> 4) & 0x10;
                            mb->reg.A = wdat & 0xFF;
                            mb->reg.F = rdat;
                            goto generic_fetch;
                        
                        instr_037:
                        case 3: // RRA
                            wdat = mb->reg.A;
                            rdat = mb->reg.F;
                            rdat &= 0x10;
                            wdat = (wdat << 8) | (wdat >> 1) | ((rdat & 0x10) << 3);
                            rdat = (wdat >> 4) & 0x10;
                            mb->reg.A = wdat & 0xFF;
                            mb->reg.F = rdat;
                            goto generic_fetch;
                        
                        instr_047:
                        case 4: // fuck
                            wdat = mb->reg.A;
                            rdat = mb->reg.F;
                            rdat &= 0x70;
                            
                            if(!(rdat & 0x40))
                            {
                                if((rdat & 0x20) || ((wdat & 0xF) > 9))
                                {
                                    wdat += 6;
                                    rdat |= 0x20;
                                }
                                
                                if((rdat & 0x10) || (((wdat >> 4) & 0x1F) > 9))
                                {
                                    wdat += 6 << 4;
                                    rdat |= 0x10;
                                }
                            }
                            
                            /*>
                            if(wdat >> 8)
                                rdat |= 0x10;
                            */
                            
                            rdat &= 0x50;
                            
                            wdat &= 0xFF;
                            if(!wdat)
                                rdat |= 0x80;
                            mb->reg.A = wdat;
                            mb->reg.F = rdat;
                            
                        #ifdef ENABLE_DAA_BYPASS
                            goto generic_fetch;
                        #else
                            return 0;
                        #endif
                        
                        instr_057:
                        case 5: // CPL A
                            mb->reg.F |= 0x60;
                            mb->reg.A = ~mb->reg.A;
                            goto generic_fetch;
                        
                        instr_067:
                        case 6: // SET Cy
                            mb->reg.F = (mb->reg.F & 0x80) | 0x10;
                            goto generic_fetch;
                        
                        instr_077:
                        case 7: // CPL Cy
                            mb->reg.F = (mb->reg.F & 0x90) ^ 0x10;
                            goto generic_fetch;
                        
                        default:
                            __builtin_unreachable();
                    }
            }
            return 0;
        
        case 1: // MOV
            if(IR != 0x76)
            {
                instr_MOV:
                {
                #if GBA
                    var vIR = IR ^ 9;
                    isrc = vIR & 7;
                    idst = (vIR >> 3) & 7;
                #else
                    isrc = column ^ 1;
                    idst = row ^ 1;
                #endif
                }
                
                if(isrc != 7)
                {
                    rdat = mb->reg.raw8[isrc];
                }
                else
                {
                    ++ncycles;
                    rdat = mch_memory_dispatch_read(mb, mb->reg.HL);
                }
                
            generic_r8_write:
                if(idst != 7)
                {
                    mb->reg.raw8[idst] = rdat;
                }
                else
                {
                    mch_memory_dispatch_write(mb, mb->reg.HL, rdat);
                    ++ncycles;
                }
                
                goto generic_fetch;
            }
            else
            {
                instr_0x76:
                goto generic_fetch_halt;
            }
        
        case 2: // ALU r8
        {
            // A is always the src and dst, thank fuck
            
            instr_ALU:
            
            isrc = F_COL ^ 1;
            
            if(isrc != 7)
            {
                rdat = mb->reg.raw8[isrc];
            }
            else
            {
                ++ncycles;
                rdat = mch_memory_dispatch_read(mb, mb->reg.HL);
            }
            
            alu_op_begin:
            rres = mb->reg.A;
            
            switch(F_ROW)
            {
                //instr_ALU_0:
                case 0: // ADD Z0HC
                    rflag = 0;
                    goto alu_case_1;
                
                //instr_ALU_1:
                case 1: // ADC Z0HC
                    rflag = mb->reg.F & 0x10;
                    
                alu_case_1:
                {
                    /*
                    printf("!   %u %02X %02X", F_ROW, rres, rdat);
                    if(F_ROW & 2)
                        printf(" (%02X)\n", rdat ^ 0xFF);
                    else
                        puts("");
                    */
                    
                    // (wdat & 0xF) < ((rres & 0xF) + Cy)
                    // (wdat & 0xF) + (~rres & 0xF) + Cy^NF) & 0x10
                    // !(((a & 0xF) + ((~b) & 0xF) + (c^1)) & 0x10)
                    
                    wdat = (rdat & 0xF) + (rres & 0xF) + (((rflag >> 4) ^ (rflag >> 6)) & 1);
                    if(!(wdat & 0x10) != !((rflag >> 6) & 1))
                        rflag |= 0x20;
                    
                    wdat += (rdat & 0xF0) + (rres & 0xF0) + ((rflag << 2) & 0x100);
                    if(wdat & 0x100)
                        rflag |= 0x10;
                    else
                        rflag &= 0xE0;
                    
                    if(F_ROW != 7)
                    {
                        /*
                        printf("! %03X %c%c%c%c\n", wdat,
                            (rflag & 0x80) ? 'Z' : '-',
                            (rflag & 0x40) ? 'N' : '-',
                            (rflag & 0x20) ? 'H' : '-',
                            (rflag & 0x10) ? 'C' : '-'
                        );
                        */
                        
                        rres = wdat;
                        
                        if(!(F_ROW & 2))
                            break;
                        
                        //rflag ^= 0x10;
                        //rflag ^= 0x20;
                        
                        break;
                    }
                    else
                    {
                        //printf("! CP %02X, %02X (%02X)\n", rres, rdat, wdat);
                        rres = wdat ^ 0x1FF;
                        if(!(rres & 0xFF))
                            rflag |= 0x80;
                        
                        rflag ^= 0x10;
                        mb->reg.F = rflag;
                        
                        goto generic_fetch;
                    }
                    
                    break;
                }
                
                //instr_ALU_2:
                case 2: // SUB Z1HC
                    rflag = 0x40;
                    rdat ^= 0xFF;
                    goto alu_case_1;
                
                //instr_ALU_7:
                case 7: // CMP Z1HC
                    //TODO: fuck this
                    /*
                    rflag = 0x50;
                    rdat ^= 0xFF;
                    goto alu_case_1;
                    */
                    
                    rflag = 0x40;
                    if(rres < rdat)
                        rflag |= 0x10;
                    if((rres & 0xF) < (rdat & 0xF))
                        rflag |= 0x20;
                    
                    if(rres == rdat)
                       rflag |= 0x80;
                    
                    mb->reg.F = rflag; 
                    
                    goto generic_fetch;
                
                //instr_ALU_3:
                case 3: // SBC Z1HC
                    rflag = (mb->reg.F & 0x10) ^ 0x40;
                    rdat ^= 0xFF;
                    goto alu_case_1;
                
                //instr_ALU_4:
                case 4: // AND Z010
                    rflag = 0x20;
                    rres &= rdat;
                    break;
                
                //instr_ALU_5:
                case 5: // XOR Z000
                    rflag = 0;
                    rres ^= rdat;
                    break;
                
                //instr_ALU_6:
                case 6: // ORR Z000
                    rflag = 0;
                    rres |= rdat;
                    break;
                
                default:
                    __builtin_unreachable();
            }
            
            if(!(rres & 0xFF))
                rflag |= 0x80;
            
            {
                hilow16_t sta;
                sta.low = rres;
                sta.high = rflag;
                mb->reg.hilo16[3] = sta;
            }
            
            goto generic_fetch;
        }
        
        case 3: // Bottom bullshit
            switch(column)
            {
                case 0: // misc junk and RET cc
                {
                    if(IR & 0x20) // misc junk
                    {
                        if(!(IR & 8)) // LDH n8
                        {
                            wdat = 0xFF00 | mch_memory_fetch_PC(mb);
                            
                            if(IR & 0x10)
                            {
                                instr_360:
                                DBGF("- /HR %04X -> ", wdat);
                                mb->reg.A = mch_memory_dispatch_read_fexx_ffxx(mb, wdat);
                                DBGF("%02X\n", mb->reg.A);
                            }
                            else
                            {
                                instr_340:
                                DBGF("- /HW %04X <- %02X\n", wdat, mb->reg.A);
                                mch_memory_dispatch_write_fexx_ffxx(mb, wdat, mb->reg.A);
                            }
                            
                            ncycles += 2;
                            goto generic_fetch;
                        }
                        else
                        {
                            //TODO: nobody uses the flags from this output, but still would be good if it actually got updated properly
                            
                            wdat = mch_memory_fetch_PC(mb);
                            if(wdat >= 0x80)
                                wdat |= 0xFF00;
                            
                            wdat = (wdat + mb->SP) & 0xFFFF;
                            
                            if(IR & 0x10) // HL = SP + e8
                            {
                                instr_370:
                                mb->reg.F &= 0xB0;
                                mb->reg.HL = wdat;
                                ncycles += 2; // ???
                            }
                            else // SP = SP + e8
                            {
                                instr_350:
                                mb->SP = wdat;
                                mb->reg.F &= 0x30;
                                ncycles += 3; // ???
                            }
                            
                            goto generic_fetch;
                        }
                    }
                    else // RET cc
                    {
                        instr_RET_cc:
                        
                        ncycles += 1; // cc_check penalty cycle
                        
                        if(!CC_CHECK)
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
                                wdat = mch_memory_dispatch_read(mb, SP++);
                                wdat |= mch_memory_dispatch_read(mb, (SP++) & 0xFFFF) << 8;
                                mb->SP = SP;
                                
                                mb->PC = wdat;
                                
                                ncycles += 2 + 1; // idk why penalty cycle
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
                                ++ncycles; // wide bus penalty
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
                        wdat = mch_memory_dispatch_read(mb, SP++);
                        wdat |= mch_memory_dispatch_read(mb, (SP++) & 0xFFFF) << 8;
                        mb->SP = SP;
                        
                        isrc = (IR >> 4) & 3;
                        if(isrc != 3)
                            mb->reg.raw16[isrc] = wdat;
                        else
                            MB_W_AF(wdat);
                        
                        goto generic_fetch;
                    }
                }
                
                case 2: // LD mem or JP cc, n16
                {
                    if(IR & 0x20) // LD mem
                    {
                        if(IR & 8)
                        {
                            wdat = mch_memory_fetch_PC_2(mb);
                            
                            if(IR & 0x10)
                            {
                                instr_372:
                                mb->reg.A = mch_memory_dispatch_read(mb, wdat);
                            }
                            else
                            {
                                instr_352:
                                mch_memory_dispatch_write(mb, wdat, mb->reg.A);
                            }
                            
                            ncycles += 2 + 1;
                        }
                        else
                        {
                            wdat = 0xFF00 | mb->reg.C;
                            
                            if(!(IR & 0x10))
                            {
                                instr_342:
                                DBGF("- /HW %04X <- %02X\n", wdat, mb->reg.A);
                                mch_memory_dispatch_write_fexx_ffxx(mb, wdat, mb->reg.A);
                            }
                            else
                            {
                                instr_362:
                                DBGF("- /HR %04X -> ", wdat);
                                mb->reg.A = mch_memory_dispatch_read_fexx_ffxx(mb, wdat);
                                DBGF("%02X\n", mb->reg.A);
                            }
                            
                            ncycles += 1;
                        }
                        
                        goto generic_fetch;
                    }
                    else // JP cc, n16
                    {
                        instr_JP_cc_n16:
                        // small optimization, no imm fetch if no match
                        
                        if(CC_CHECK)
                            goto generic_jp_abs;
                        else
                        {
                            mb->PC = (mb->PC + 2) & 0xFFFF;
                            ncycles += 2;
                            goto generic_fetch;
                        }
                    }
                }
                
                case 3: // junk
                {
                    switch(F_ROW)
                    {
                        instr_303:
                        case 0: // JP n16
                        generic_jp_abs:
                        {
                            wdat = mch_memory_fetch_PC_2(mb);
                            mb->PC = wdat;
                            ncycles += 2 + 1; // idk why penalty cycle
                            goto generic_fetch;
                        }
                        
                        instr_313:
                        case 1: // $CB
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
                
                case 4: // CALL cc, n16
                {
                    // small optimization, do not fetch imm unless match
                    
                    if(!(IR & 32))
                    {
                        instr_CALL_cc_n16:
                        
                        if(CC_CHECK)
                            goto generic_call;
                        else
                        {
                            mb->PC = (mb->PC + 2) & 0xFFFF;
                            ncycles += 2;
                            goto generic_fetch;
                        }
                    }
                    else
                    {
                        return 0; //ILL
                    }
                }
                    
                case 5: // PUSH r16 / CALL n16
                {
                    if(!(IR & 8)) // PUSH r16
                    {
                        isrc = (IR >> 4) & 3;
                        if(isrc != 3)
                            wdat = mb->reg.raw16[isrc];
                        else
                            wdat = MB_R_AF;
                        
                    generic_push:
                        if(1)
                        {
                            var SP = mb->SP;
                            mch_memory_dispatch_write(mb, (--SP) & 0xFFFF, wdat >> 8);
                            mch_memory_dispatch_write(mb, (--SP) & 0xFFFF, wdat & 0xFF);
                            mb->SP = SP;
                            ncycles += 2 + 1; // IDU can't pre-decrement, one penalty cycle
                        }
                        
                        goto generic_fetch;
                    }
                    else
                    {
                        if(row == 1) // CALL n16
                        {
                        generic_call:
                        {
                            var PC = mb->PC;
                            wdat = mch_memory_fetch_PC_2(mb);
                            mb->PC = wdat;
                            wdat = (PC + 2) & 0xFFFF;
                            ncycles += 2;
                            
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
                    ++ncycles;
                    rdat = mch_memory_fetch_PC(mb);
                    goto alu_op_begin;
                }
                
                case 7: // RST
                {
                    instr_3x7:
                    wdat = mb->PC;
                    mb->PC = IR & (7 << 3);
                    goto generic_push;
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
        //mb->HALTING = !(mb->IE & mb->IF & 0x1F);
        mb->HALTING = 1;
        
        var PC = mb->PC;
        mb->IR.raw = mch_memory_fetch_PC(mb);
        /*
        mb->PC = PC;
        if(mb->HALTING)
            mb->IR.raw = 0x00; // NOP
        */
        return ncycles + 1;
    }
    
    handle_cb:
    if(1)
    {
        ++ncycles;
        var CBIR = mch_memory_fetch_PC(mb);
        
    #if CONFIG_DBG
        if(_IS_DBG)
        {
            printf("               (%01o:%01o:%01o) ", CBIR >> 6, CBIR & 7, (CBIR >> 3) & 7);
            disasm_CB(mb, CBIR);
        }
    #endif
    
        isrc = (CBIR >> 3) & 7;
        idst = (CBIR & 7) ^ 1;
        
        if(idst != 7)
            rdat = mb->reg.raw8[idst];
        else
        {
            ++ncycles;
            rdat = mch_memory_dispatch_read(mb, mb->reg.HL);
        }
        
        switch(CBIR >> 6)
        {
            case 0: // CB OP
                rflag = mb->reg.F;
                rflag &= 0x10;
                
                switch(isrc)
                {
                    case 0: // RLC
                        rdat = (rdat << 1) | (rdat >> 7);
                        rflag = (rdat >> 4) & 0x10;
                        break;
                    
                    case 1: // RRC
                        rdat = (rdat << 7) | (rdat >> 1);
                        rflag = (rdat >> 3) & 0x10;
                        break;
                    
                    case 2: // RL
                        rdat = (rdat << 1) | ((rflag >> 4) & 1);
                        rflag = (rdat >> 4) & 0x10;
                        break;
                    
                    case 3: // RR
                        rdat = (rdat << 8) | (rdat >> 1) | ((rflag & 0x10) << 3);
                        rflag = (rdat >> 4) & 0x10;
                        break;
                    
                    case 4: // LSL
                        rdat = (rdat << 1);
                        rflag = (rdat >> 4) & 0x10;
                        break;
                    
                    case 5: // ASR
                        rflag = (rdat & 1) << 4;
                        rdat = (rdat >> 1) | (rdat & 0x80);
                        break;
                    
                    case 6: // SWAP
                        rflag = 0;
                        rdat = (rdat >> 4) | (rdat << 4);
                        break;
                    
                    case 7: // LSR
                        rflag = (rdat & 1) << 4;
                        rdat = (rdat >> 1);
                        break;
                }
                
                rdat &= 0xFF;
                if(!rdat)
                    rflag |= 0x80;
                
                mb->reg.F = rflag;
                goto cb_writeback;
            
            case 1: // BTT
                rflag = mb->reg.F;
                
                if(rdat & (1 << isrc))
                    rflag = (rflag & 0x10) | 0x20;
                else
                    rflag = (rflag & 0x10) | 0xA0;
                
                mb->reg.F = rflag;
                
                goto generic_fetch;
            
            case 2: // RES
                rdat &= ~(1 << isrc);
                goto cb_writeback;
            
            case 3: // SET
                rdat |= (1 << isrc);
                goto cb_writeback;
        }
        
        return 0; // WTF
        
        cb_writeback:
        
        if(idst != 7)
            mb->reg.raw8[idst] = rdat;
        else
        {
            mch_memory_dispatch_write(mb, mb->reg.HL, rdat);
            ++ncycles;
        }
        
        goto generic_fetch;
    }
    return 0; // WTF
}

void micache_invalidate(struct mb_mi_cache* __restrict mic)
{
    word counts = MICACHE_R_VALUE(0x10000);
    
    word i = 0;
    do
    {
        mic->mc_execute[i] = 0;
        mic->mc_write[i] = 0;
        mic->mc_read[i] = 0;
    }
    while(++i < counts);
}

void micache_invalidate_range(struct mb_mi_cache* __restrict mic, word start, word end)
{
    word ends = MICACHE_R_VALUE(end - 1);
    
    word i = MICACHE_R_VALUE(start);
    do
    {
        mic->mc_execute[i] = 0;
        mic->mc_write[i] = 0;
        mic->mc_read[i] = 0;
    }
    while(++i <= ends);
}
