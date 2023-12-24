#include <assert.h>
#include <stdint.h>
#include <stddef.h>

#include "fabric.h"
#include "dbg.h"

#if CONFIG_ENABLE_LRU
#include "lru.h"
#endif


PGB_FUNC void pgf_timer_update_internal(struct pgf_userdata_t* __restrict ud, word ticks)
{
    //if(!(ud->TIMER_CNT & 4))
    //    return;
    
    var nres = ticks + ud->TIMER_SUB;
    // 0, 4, 16, 64
    
    var ncnt = 1 << ((ud->TIMER_CNT & 3) << 1);
    if(ncnt & 1)
        ncnt <<= 8;
    
    /*
    var ncnt;
    switch(ud->TIMER_CNT & 3)
    {
        case 0:
            ncnt = 256;
            break;
        case 1:
            ncnt = 4;
            break;
        case 2:
            ncnt = 16;
            break;
        case 3:
            ncnt = 64;
            break; 
        
        default:
            __builtin_unreachable();
    }
    */
    
    
    if(nres >= ncnt)
    {
        var tv = ud->TIMER_ACCUM;
        
        do
        {
            if((++tv) >> 8)
            {
                tv = ud->TIMER_LOAD;
                ud->mb->IF |= 4;
            }
            
            nres -= ncnt;
        }
        while(nres >= ncnt);
        
        ud->TIMER_ACCUM = tv;
    }
    
    ud->TIMER_SUB = nres;
}

PGB_FUNC const r8* pgf_resolve_ROM(void* userdata, word addr, word bank)
{
    return pgf_resolve_ROM_internal(userdata, addr, bank);
}

//#include "profi.h"

PGB_FUNC const r8* __restrict pgf_resolve_octant(void* userdata, word addr_oct)
{
    USE_UD;
    
    const r8* __restrict SRC = 0;
    
    word wb = addr_oct << 8;
    
    switch((addr_oct >> 4) & 0xF)
    {
        case 0:
        case 1:
        case 2:
        case 3:
            SRC = pgf_resolve_ROM(userdata, wb, 0);
            break;
        case 4:
        case 5:
        case 6:
        case 7:
            SRC = pgf_resolve_ROM(userdata, wb, ud->mb->mi->BANK_ROM);
            break;
        case 8:
        case 9:
            SRC = &ud->mb->mi->VRAM[(ud->mb->mi->BANK_VRAM << 13) + (wb & 0x1F00)];
            break;
        case 12:
        case 14:
            SRC = &ud->mb->mi->WRAM[wb & 0xF00];
            break;
        case 13:
        case 15:
        {
            var bank = ud->mb->mi->BANK_WRAM;
            if(!bank)
                bank = 1;
            SRC = &ud->mb->mi->WRAM[(bank << 12) + (wb & 0xF00)];
            break;
        }
        default:
            SRC = 0;
            break;
    }
    
    return SRC;
}

PGB_FUNC word pgf_cb_IO_(void* userdata, word addr, word data, word type)
{
    USE_UD;
    
    assert(addr >= 0xFE00 && addr < 0xFF80);
    
    if((addr >> 8) == 0xFF)
    {
        var reg = addr & 0xFF;
        var* __restrict rv;
        
        if(reg < 0x10) // random garbage
        {
            if(reg == 0)
            {
                if(type)
                {
                    ud->JOYP = (data & 0x30);
                    return data;
                }
                
                var jpsel = (ud->JOYP & 0x30) | 0xCF;
                if(!(jpsel & 0x10))
                    jpsel &= ~((ud->JOYP_RAW >> 4) & 0xF);
                if(!(jpsel & 0x20))
                    jpsel &= ~(ud->JOYP_RAW & 0xF);
                
                return jpsel;
            }
            //else if(reg == 1) { if(type) putchar(data); }
            else if(reg == 4)
            {
                if(type)
                {
                    ud->mb->DIV = 0;
                #if CONFIG_APU_ENABLE
                    ud->apu->CTR_DIV = 0;
                #endif
                    return 0;
                }
                
                return (ud->mb->DIV >> 6) & 0xFF;
            }
            else if(reg == 5)
            {
                if(type)
                {
                    ud->TIMER_ACCUM = data;
                    return data;
                }
                
                return ud->TIMER_ACCUM;
            }
            else if(reg == 6)
            {
                if(type)
                {
                    ud->TIMER_LOAD = data;
                    return data;
                }
                
                return ud->TIMER_LOAD;
            }
            else if(reg == 7)
            {
                if(type)
                {
                    if(!(ud->TIMER_CNT & 4))
                        ud->TIMER_SUB = -2;
                    //ud->TIMER_ACCUM = ud->TIMER_LOAD;
                    ud->TIMER_CNT = data & 7;
                    return data;
                }
                
                return ud->TIMER_CNT | 0xF8;
            }
            else if(reg == 0x0F)
            {
                if(type)
                {
                    ud->mb->IF = data & 0x1F;
                    return data;
                }
                
                return ud->mb->IF | 0xE0;
            }
        }
        else if(reg < 0x27)
        {
        #if CONFIG_APU_ENABLE
            if(type)
                apu_write(ud->apu, reg, data);
            else
                return apu_read(ud->apu, reg);
        #else
            return 0xFF;
        #endif
        }
        else if(reg < 0x30)
        {
            return 0xFF;
        }
        else if(reg < 0x40)
        {
        #if CONFIG_APU_ENABLE
            if(type)
                apu_write_wave(ud->apu, reg, data);
            else
                return apu_read_wave(ud->apu, reg);
        #else
            return 0xFF;
        #endif
        }
        else if(reg < 0x4C) // PPU
        {
            reg &= 0xF;
            
            switch(reg)
            {
                case 0: // LCDC
                    rv = &ud->ppu->rLCDC;
                    if(type)
                    {
                        if(!(data & 0x80))
                        {
                            ppu_turn_off(ud->ppu);
                        }
                        else
                        {
                            if(!(ud->ppu->rLCDC & 0x80))
                                ppu_turn_on(ud->ppu);
                        }
                    }
                    break;
                case 1: // STAT
                    if(type)
                    {
                        ud->ppu->rSTAT = (ud->ppu->rSTAT & 0x87) | (data & 0x78);
                        return data;
                    }
                    
                    return ud->ppu->rSTAT | 0x80;
                case 2: // SCY
                    rv = &ud->ppu->rSCY;
                    break;
                case 3: // SCX
                    rv = &ud->ppu->rSCX;
                    break;
                case 4: // LY
                #if !CONFIG_LYC_90
                    return ud->ppu->state.scanY;
                #else
                    return 0x90;
                #endif
                case 5: // LYC
                    if(type)
                    {
                        ud->ppu->rLYC = data & 0xFF;
                        ppu_on_write_LYC(ud->ppu);
                        return data;
                    }
                    else
                    {
                        return ud->ppu->rLYC;
                    }
                case 6: // OAMDMA
                {
                    if(!type)
                        return 0xFF;
                    
                    /*
                    if(data < 0xC0 || data > 0xDF)
                    {
                        printf("Triggered bad OAMDMA: %02X\n", data);
                        printf(" A: %02X\n", ud->mb->reg.A);
                        if(!(ud->mb->reg.F & 0xF))
                            printf(" F: %01Xx\n", ud->mb->reg.F >> 4);
                        else
                            printf(" F: %02X (!!!)\n", ud->mb->reg.F);
                        
                        printf("BC: %04X\n", ud->mb->reg.BC);
                        printf("DE: %04X\n", ud->mb->reg.DE);
                        printf("HL: %04X\n", ud->mb->reg.HL);
                        printf("PC: %04X\n", ud->mb->PC);
                        printf("SP: %04X\n", ud->mb->SP);
                        printf("IR: %02X %u\n", ud->mb->IR.low, ud->mb->IR.high);
                        printf("IRQ %02X & %02X %u:%u\n", ud->mb->IE, ud->mb->IF, ud->mb->IME, ud->mb->IME_ASK);
                        
                        puts("");
                        
                        if(profi_depth)
                        {
                            DWORD di;
                            printf("%s", "Call stack:\n");
                            
                            for(di = 0; di != profi_depth; di++)
                            {
                                printf("- %08lX -> %08lX\n", profi_callchain[di].HighPart, profi_callchain[di].LowPart);
                            }
                        }
                        else
                        {
                            puts("Bad call stack: no depth");
                        }
                        
                        puts("===");
                    }
                    */
                    
                    //TODO: OAMDMA
                    //ud->_debug = data;
                    var wb = data << 8;
                    var w;
                    r8* __restrict OAM = (r8* __restrict)(size_t)&ud->ppu->OAM[0];
                    const r8* __restrict SRC = pgf_resolve_octant(userdata, data & 0xFF);
                    if(SRC)
                    {
                        for(w = 0; w != 160; ++w)
                        {
                            OAM[w] = SRC[w];
                        }
                    }
                    return 0xFF;
                }
                case 7: // BGP
                    rv = &ud->ppu->rBGP;
                    break;
                case 8: // OBP0
                    rv = &ud->ppu->rOBP0;
                    break;
                case 9: // OBP1
                    rv = &ud->ppu->rOBP1;
                    break;
                
                case 0xA: // WY
                    rv = &ud->ppu->rWY;
                    break;
                case 0xB: // WX
                    rv = &ud->ppu->rWX;
                    break;
                
                default:
                    return 0xFF;
            }
            
            if(type)
            {
                *rv = data & 0xFF;
                return data;
            }
            
            return *rv & 0xFF;
        }
        else if(reg < 0x68) // misc and CGB garbage
        {
            if(reg == 0x4F)
            {
            #if CONFIG_FORCE_ENABLE_CGB
                if(type)
                {
                    ud->mb->mi->BANK_VRAM = data & 1;
                    //micache_invalidate(&ud->mb->micache);
                    micache_invalidate_range(&ud->mb->micache, 0x8000, 0x9FFF);
                    return data;
                }
                
                return ud->mb->mi->BANK_VRAM | 0xFE;
            #endif
            }
            else if(reg == 0x50)
            {
                if(type)
                {
                    #if CONFIG_DBG
                    _IS_DBG = 1;
                    #endif
                    
                    //micache_invalidate(&ud->mb->micache);
                    micache_invalidate_range(&ud->mb->micache, 0x0000, 0x7FFF);
                    
                #if CONFIG_BOOTMEME
                    if(ud->mb->mi->dispatch_BOOTROM)
                        ud->mb->mi->dispatch_BOOTROM(userdata, 0, 0, MI_LONGDISPATCH_BOOTROM_LOCK);
                    ud->mb->mi->dispatch_BOOTROM = 0; // disable iboot interface
                #endif
                    
                    return data;
                }
                
                return 0xFF;
            }
        #if CONFIG_FORCE_ENABLE_CGB
            else if(reg == 0x51)
            {
                if(type)
                    ud->GDMA_SRC = (ud->GDMA_SRC & 0x00F0) | ((data & 0xFF) << 8);
                
                return 0xFF;
            }
            else if(reg == 0x52)
            {
                if(type)
                    ud->GDMA_SRC = (ud->GDMA_SRC & 0xFF00) | ((data & 0xF0) << 0);
                
                return 0xFF;
            }
            else if(reg == 0x53)
            {
                if(type)
                    ud->GDMA_DST = (ud->GDMA_DST & 0x00F0) | ((data & 0x1F) << 8);
                
                return 0xFF;
            }
            else if(reg == 0x54)
            {
                if(type)
                    ud->GDMA_DST = (ud->GDMA_DST & 0xFF00) | ((data & 0xF0) << 0);
                
                return 0xFF;
            }
            else if(reg == 0x55)
            {
                if(type)
                {
                    ud->GDMA_CNT = data & 0xFF;
                    
                    {
                        var count = (ud->GDMA_CNT & 0x7F) + 1;
                        
                        while(count)
                        {
                            --count;
                            const r8* __restrict SRC = pgf_resolve_octant(userdata, (ud->GDMA_SRC >> 8) & 0xFF);
                            
                            if(SRC)
                            {
                                SRC += (ud->GDMA_SRC & 0x00F0);
                                
                                r8* __restrict DST = &ud->mb->mi->VRAM[(ud->mb->mi->BANK_VRAM << 13) + (ud->GDMA_DST & 0x1FF0)];
                                
                                var count_sub = 0x10;
                                
                                do
                                    *(DST++) = *(SRC++);
                                while(--count_sub);
                            }
                            
                            ud->GDMA_DST += 0x10;
                            ud->GDMA_SRC += 0x10;
                        }
                        
                        ud->GDMA_CNT = 0;
                    }
                }
                
                /*
                if(ud->GDMA_CNT & 0x80)
                    return ud->GDMA_CNT;
                else
                    return 0xFF;
                */
                
                return 0xFF;
            }
        #endif
        }
        else // undocumented CGB garbage
        {
            if(reg == 0x68)
            {
                if(type)
                {
                    ud->ppu->rBGPI = data | 0x40;
                    return data;
                }
                
                return ud->ppu->rBGPI;
            }
            else if(reg == 0x69)
            {
                var idx = ud->ppu->rBGPI & 0x3F;
                
                if(type)
                {
                    if(ud->ppu->rBGPI & 0x80)
                        ud->ppu->rBGPI = (idx + 1) | 0xC0;
                    
                    if(!(idx & 1))
                    {
                        ud->ppu->BGP[idx >> 1] = (ud->ppu->BGP[idx >> 1] & 0xFF00) | data;
                    }
                    else
                    {
                        ud->ppu->BGP[idx >> 1] = (ud->ppu->BGP[idx >> 1] & 0x00FF) | (data << 8);
                    }
                    
                    return data;
                }
                
                if(!(idx & 1))
                    return ud->ppu->BGP[idx >> 1] & 0xFF;
                else
                    return ud->ppu->BGP[idx >> 1] >> 8;
            }
            else if(reg == 0x6A)
            {
                if(type)
                {
                    ud->ppu->rOBPI = data | 0x40;
                    return data;
                }
                
                return ud->ppu->rOBPI;
            }
            else if(reg == 0x6B)
            {
                var idx = ud->ppu->rOBPI & 0x3F;
                
                if(type)
                {
                    if(ud->ppu->rOBPI & 0x80)
                        ud->ppu->rOBPI = (idx + 1) | 0xC0;
                    
                    if(!(idx & 1))
                    {
                        ud->ppu->OBP[idx >> 1] = (ud->ppu->OBP[idx >> 1] & 0xFF00) | data;
                    }
                    else
                    {
                        ud->ppu->OBP[idx >> 1] = (ud->ppu->OBP[idx >> 1] & 0x00FF) | (data << 8);
                    }
                    
                    return data;
                }
                
                if(!(idx & 1))
                    return ud->ppu->OBP[idx >> 1] & 0xFF;
                else
                    return ud->ppu->OBP[idx >> 1] >> 8;
            }
            else if(reg == 0x70)
            {
            #if CONFIG_FORCE_ENABLE_CGB
                if(type)
                {
                    ud->mb->mi->BANK_WRAM = data & 7;
                    //micache_invalidate(&ud->mb->micache);
                    micache_invalidate_range(&ud->mb->micache, 0xD000, 0xDFFF);
                    micache_invalidate_range(&ud->mb->micache, 0xE000, 0xFDFF);
                    return data;
                }
                
                return ud->mb->mi->BANK_WRAM | 0xF8;
            #endif
            }
        #if CONFIG_BOOTMEME
            else if(ud->mb->mi->dispatch_BOOTROM && reg >= 0x74 && reg <= 0x7C)
            {
                reg -= 0x74;
                if(reg < 8)
                {
                    if(type)
                        ud->mb->mi->BOOTROM_DATA[reg] = data;
                    else
                        return ud->mb->mi->BOOTROM_DATA[reg];
                    
                    return data;
                }
                else if(type && ud->mb->mi->dispatch_BOOTROM)
                {
                    r32 res = ud->mb->mi->dispatch_BOOTROM
                    (
                        userdata,
                        mi_iboot_get_offset(ud->mb->mi),
                        mi_iboot_get_data(ud->mb->mi),
                        data
                    );
                    
                    mi_iboot_set_data(ud->mb->mi, res);
                }
            }
        #endif
        }
        
        return 0xFF;
    }
    else if((addr >> 8) == 0xFE)
    {
        //TODO: wtf is this even
        r8* __restrict rv = (r8* __restrict)(size_t)&ud->ppu->OAM[addr & 0xFF];
        
        if(type)
        {
            *rv = data;
            return data;
        }
        
        return *rv & 0xFF;
    }
    
    assert(!"bad reg");
    return 0xFF;
}

static const r8 mappers[0x20] PGB_DATA =
{
    0, 1, 1, 1,
    0, 2, 2, 0,
    0, 0, 0, 4,
    4, 4, 0, 3,
    3, 3, 3, 3,
    0, 0, 0, 0,
    0, 5, 5, 5,
    5, 5, 5, 0
};

PGB_FUNC word pgf_cb_ROM_(void* userdata, word addr, word data, word type)
{
    USE_UD;
    
    var mapper = ud->mb->mi->ROM_MAPPER;
    assert(mapper < 0x20);
    var lut = mappers[mapper];
    switch(lut)
    {
        case 0:
            break;
        case 1:
            switch((addr >> 13) & 3)
            {
                case 0:
                    break;
                case 1:
                    ud->mb->mi->BANK_ROM = (data & 0x1F);
                    if(!ud->mb->mi->BANK_ROM)
                        ud->mb->mi->BANK_ROM = 1;
                    
                    ud->mb->mi->BANK_ROM &= ud->mb->mi->N_ROM - 1;
                    
                    micache_invalidate_range(&ud->mb->micache, 0x4000, 0x7FFF);
                    break;
                case 2:
                    if(data && (ud->mb->mi->N_ROM > 0x20))
                        assert(!"big MBC1 is not supported yet");
                    break;
                case 3:
                    if(ud->mb->mi->N_ROM > 0x20)
                        assert(!"banking mode sel for MBC1 is not supported yet");
                    break;
                    
            }
            break;
        case 2:
            if(!(addr & 0x4000))
            {
                if(addr & 0x100)
                {
                    ud->mb->mi->BANK_ROM = (data);
                    if(!ud->mb->mi->BANK_ROM)
                        ud->mb->mi->BANK_ROM = 1;
                    
                    ud->mb->mi->BANK_ROM &= ud->mb->mi->N_ROM - 1;
                    
                    micache_invalidate_range(&ud->mb->micache, 0x4000, 0x7FFF);
                }
            }
            break;
        case 3:
            switch((addr >> 13) & 3)
            {
                case 0:
                    break;
                case 1:
                    ud->mb->mi->BANK_ROM = (data & 0x7F);
                    if(!ud->mb->mi->BANK_ROM)
                        ud->mb->mi->BANK_ROM = 1;
                    
                    ud->mb->mi->BANK_ROM &= ud->mb->mi->N_ROM - 1;
                    
                    micache_invalidate_range(&ud->mb->micache, 0x4000, 0x7FFF);
                    break;
                case 2:
                    //assert(!"MBC3 RAM/reg sel is not supported yet");
                    ud->mb->mi->BANK_SRAM = (data & 15) & (ud->mb->mi->N_SRAM - 1);
                    micache_invalidate_range(&ud->mb->micache, 0xA000, 0xBFFF);
                    break;
                case 3:
                    //assert(!"MBC3 unsupported write");
                    break;
                    
            }
            break;
        case 5:
            switch((addr >> 12) & 7)
            {
                case 0:
                case 1:
                    break;
                case 2:
                    ud->mb->mi->BANK_ROM = (data & 0xFF);
                    if(!ud->mb->mi->BANK_ROM)
                        ud->mb->mi->BANK_ROM = 1;
                    
                    ud->mb->mi->BANK_ROM &= ud->mb->mi->N_ROM - 1;
                    
                    micache_invalidate_range(&ud->mb->micache, 0x4000, 0x7FFF);
                    break;
                case 3:
                    if(data)
                        assert(!"MBC5 big ROM is not supported yet");
                    break;
                case 4:
                case 5:
                    ud->mb->mi->BANK_SRAM = (data & 15) & (ud->mb->mi->N_SRAM - 1);
                    micache_invalidate_range(&ud->mb->micache, 0xA000, 0xBFFF);
                    break;
                default:
                    //assert(!"MBC5 unsupported write");
                    break;
                    
            }
            break;
        default:
            assert(!"bad mapper LUT");
    }
    
    //micache_invalidate(&ud->mb->micache);
    //micache_invalidate_range(&ud->mb->micache, 0x4000, 0x7FFF);
    
    return 0xFF;
}

#if CONFIG_BOOTMEME
PGB_FUNC r32 pgf_cb_BOOTROM(void* userdata, r32 addr, r32 data, word type)
{
    USE_UD;
    
    const r8* __restrict ptr = (const r8* __restrict)ud->mb->mi->userdata_BOOTROM;
    
    if(type == MI_LONGDISPATCH_READ_8 && ptr)
    {
        return ptr[addr];
    }
    else if(type == MI_LONGDISPATCH_READ_32 && ptr)
    {
        return *(const r32* __restrict)&ptr[addr];
    }
    else if(type == MI_LONGDISPATCH_BOOTROM_LOCK)
    {
        micache_invalidate(&ud->mb->micache);
    }
    
    return ~0;
}
#endif

#if CONFIG_ENABLE_LRU
PGB_FUNC const r8* pgf_cb_ROM_LRU_(void* userdata, word addr, word bank)
{
    USE_UD;
    
    struct mi_dispatch_ROM_Bank* dispatch_ROM_Bank = (struct mi_dispatch_ROM_Bank*)ud->mb->mi->userdata_ROM_Bank;
    
    if(addr < 0x4000)
        bank = 0;
    
    var isnew = 0;
    struct lru_slot* slot = lru_get_write(dispatch_ROM_Bank->lru, addr, bank, &isnew);
    if(slot)
    {
        if(isnew)
        {
            var i;
            //printf("! LRU update %02X:%04X\n", bank, addr);
            
            micache_invalidate_range(&ud->mb->micache, slot->token_address, slot->token_address + 2);
            micache_invalidate_range(&ud->mb->micache, addr, addr + 2);
            
            lru_update_slot(slot, addr, bank);
            
            const r8* dataptr = dispatch_ROM_Bank->dispatch(dispatch_ROM_Bank->userdata, addr, bank);
            
            for(i = 0; i < (1 << MICACHE_R_BITS); i++)
                slot->data[i] = dataptr[i];
        }
        
        return slot->data;
    }
    
    return 0;
}
#endif
