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
                    }
                    else
                    {
                        return ud->ppu->rLYC;
                    }
                case 6: // OAMDMA
                {
                    //TODO: OAMDMA
                    var wb = data << 8;
                    var w;
                    r8* __restrict OAM = (r8* __restrict)(size_t)ud->ppu->OAM;
                    const r8* __restrict SRC;
                    switch((data >> 4) & 0xF)
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
                if(type)
                {
                    ud->mb->mi->BANK_VRAM = data & 1;
                    //micache_invalidate(&ud->mb->micache);
                    micache_invalidate_range(&ud->mb->micache, 0x8000, 0x9FFF);
                    return data;
                }
                
                return ud->mb->mi->BANK_VRAM | 0xFE;
            }
            else if(reg == 0x50)
            {
                if(type)
                {
                    #if CONFIG_DBG
                    _IS_DBG = 1;
                    #endif
                    
                    //micache_invalidate(&ud->mb->micache);
                    micache_invalidate_range(&ud->mb->micache, 0x0000, 0x0FFF);
                    return data;
                }
                
                return 0xFF;
            }
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
                if(type)
                {
                    ud->mb->mi->BANK_WRAM = data & 7;
                    //micache_invalidate(&ud->mb->micache);
                    micache_invalidate_range(&ud->mb->micache, 0xD000, 0xDFFF);
                    return data;
                }
                
                return ud->mb->mi->BANK_WRAM | 0xF8;
            }
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

static const word mappers[0x20] PGB_DATA =
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
                    ud->mb->mi->BANK_ROM = (data & 0x1F) & (ud->mb->mi->N_ROM - 1);
                    if(!ud->mb->mi->BANK_ROM)
                        ud->mb->mi->BANK_ROM = 1;
                    
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
                    ud->mb->mi->BANK_ROM = (data) & (ud->mb->mi->N_ROM - 1);
                    if(!ud->mb->mi->BANK_ROM)
                        ud->mb->mi->BANK_ROM = 1;
                    
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
                    ud->mb->mi->BANK_ROM = (data & 0x1F) & (ud->mb->mi->N_ROM - 1);
                    if(!ud->mb->mi->BANK_ROM)
                        ud->mb->mi->BANK_ROM = 1;
                    
                    micache_invalidate_range(&ud->mb->micache, 0x4000, 0x7FFF);
                    break;
                case 2:
                    assert(!"MBC3 RAM/reg sel is not supported yet");
                    break;
                case 3:
                    assert(!"MBC3 unsupported write");
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
                    ud->mb->mi->BANK_ROM = (data & 0xFF) & (ud->mb->mi->N_ROM - 1);
                    if(!ud->mb->mi->BANK_ROM)
                        ud->mb->mi->BANK_ROM = 1;
                    
                    micache_invalidate_range(&ud->mb->micache, 0x4000, 0x7FFF);
                    break;
                case 3:
                    if(data)
                        assert(!"MBC5 big ROM is not supported yet");
                    break;
                case 4:
                case 5:
                    ud->mb->mi->BANK_SRAM = (data & 3) & (ud->mb->mi->N_SRAM - 1);
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
