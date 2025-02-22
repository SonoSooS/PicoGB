
#include <assert.h>
#include <stdint.h>
#include <stddef.h>

#include "fabric.h"
#include "dbg.h"

#if CONFIG_ENABLE_LRU
#include "lru.h"
#endif


PGB_FUNC __attribute__((optimize("O2"))) void pgf_timer_update_internal(struct pgf_userdata_t* __restrict ud, word ticks)
{
    var divider_sel = ((ud->TIMER_CNT + 3) & 3) << 1;
    var step_period = 4 << divider_sel;
    COMPILER_VARIABLE_BARRIER(step_period);
    
    var cycles_left = ticks + ud->TIMER_SUB;
    COMPILER_VARIABLE_BARRIER(cycles_left);
    
    if(COMPILER_LIKELY(cycles_left < step_period))
    {
        // nothing
    }
    else
    {
        var timer_value = ud->TIMER_ACCUM;
        var IF = 0;
        
        for(;;)
        {
            if(COMPILER_LIKELY((++timer_value) <= 0xFF))
            {
                // nothing
            }
            else
            {
                timer_value = ud->TIMER_LOAD;
                IF = 4;
            }
            
            cycles_left -= step_period;
            
            if(COMPILER_LIKELY(cycles_left < step_period))
                break;
        }
        
        if(!IF)
            ;
        else
            ud->mb->IF |= IF;
        
        ud->TIMER_ACCUM = timer_value;
    }
    
    ud->TIMER_SUB = cycles_left;
}

PGB_FUNC const r8* pgf_resolve_ROM(void* userdata, word addr, word bank)
{
    return pgf_resolve_ROM_internal(userdata, addr, bank);
}

//#include "profi.h"

PGB_FUNC const r8* __restrict pgf_resolve_octant(void* userdata, word addr_oct)
{
    USE_UD;
    
    const r8* __restrict SRC = NULL;
    
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
            SRC = NULL;
            break;
    }
    
    return SRC;
}

PGB_FUNC word pgf_cb_IO_(void* userdata, word addr, word data, word type)
{
    USE_UD;
    
    assert(addr <= 0x7F);
    
    if(1)
    {
        var reg = addr;
        var* __restrict rv;
        
        if(reg < 0x10) // random garbage
        {
            if(reg == 0)
            {
                if(MB_TYPE_IS_WRITE(type))
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
            else if(reg == 1)
            {
                if(MB_TYPE_IS_WRITE(type))
                {
                    //putchar(data);
                    ud->SB = data;
                }
                
                return ud->SB;
            }
            else if(reg == 2)
            {
                if(MB_TYPE_IS_WRITE(type))
                {
                    ud->SC = data;
                }
                
                return ud->SC |
            #if CONFIG_IS_CGB
                0x7C
            #else
                0x7E
            #endif
                ;
            }
            else if(reg == 4)
            {
                if(MB_TYPE_IS_WRITE(type))
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
                if(MB_TYPE_IS_WRITE(type))
                {
                    ud->TIMER_ACCUM = data;
                    return data;
                }
                
                return ud->TIMER_ACCUM;
            }
            else if(reg == 6)
            {
                if(MB_TYPE_IS_WRITE(type))
                {
                    ud->TIMER_LOAD = data;
                    return data;
                }
                
                return ud->TIMER_LOAD;
            }
            else if(reg == 7)
            {
                if(MB_TYPE_IS_WRITE(type))
                {
                    if(!(ud->TIMER_CNT & 4))
                        //ud->TIMER_SUB = -2;
                        ud->TIMER_SUB = 0;
                    //ud->TIMER_ACCUM = ud->TIMER_LOAD;
                    ud->TIMER_CNT = data & 7;
                    return data;
                }
                
                return ud->TIMER_CNT | 0xF8;
            }
            else if(reg == 0x0F)
            {
                if(MB_TYPE_IS_WRITE(type))
                {
                    ud->mb->IF = data & 0x1F;
                    return data;
                }
                
                return ud->mb->IF | 0xE0;
            }
        }
        else if(reg < 0x27)
        {
        #if CONFIG_APU_ENABLE_PARTIAL
        #if !CONFIG_APU_ENABLE
            if(ud->apu != NULL)
        #endif
            {
                if(MB_TYPE_IS_WRITE(type))
                    apu_write(ud->apu, reg, data);
                else
                    return apu_read(ud->apu, reg);
            }
        #endif
            return 0xFF;
        }
        else if(reg < 0x30)
        {
            return 0xFF;
        }
        else if(reg < 0x40)
        {
        #if CONFIG_APU_ENABLE_PARTIAL
        #if !CONFIG_APU_ENABLE
            if(ud->apu != NULL)
        #endif
            {
                if(MB_TYPE_IS_WRITE(type))
                    apu_write_wave(ud->apu, reg, data);
                else
                    return apu_read_wave(ud->apu, reg);
            }
        #endif
            return 0xFF;
        }
        else if(reg < 0x4C) // PPU
        {
            reg &= 0xF;
            
            switch(reg)
            {
                case 0: // LCDC
                    rv = &ud->ppu->rLCDC;
                    if(MB_TYPE_IS_WRITE(type))
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
                    if(MB_TYPE_IS_WRITE(type))
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
                    if(MB_TYPE_IS_WRITE(type))
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
                    if(!MB_TYPE_IS_WRITE(type))
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
                    if(SRC != NULL)
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
            
            // Generic handler for full read-write regs
            
            if(MB_TYPE_IS_WRITE(type))
            {
                *rv = data & 0xFF;
                return data;
            }
            else
            {
                return *rv & 0xFF;
            }
        }
        else if(reg < 0x68) // misc and CGB garbage
        {
            if(reg == 0x4C)
            {
            #if CONFIG_FORCE_ENABLE_CGB
                if(MB_TYPE_IS_WRITE(type))
                {
                    if(!(ud->CGB_MODE & 2))
                    {
                        ud->CGB_MODE = (ud->CGB_MODE & 2) | (~2 & data);
                        ud->ppu->is_cgb = (data & 12) != 4;
                    }
                    return data;
                }
                else
                {
                    return ud->CGB_MODE | 0xF2;
                }
            #endif
            }
            else if(reg == 0x4D)
            {
            #if CONFIG_FORCE_ENABLE_CGB
                if(MB_TYPE_IS_WRITE(type))
                {
                    if(ud->ppu->is_cgb)
                    {
                        ud->CGB_SPEED = (ud->CGB_SPEED & 0xFE) | (data & 1);
                        return data;
                    }
                }
                else if(ud->ppu->is_cgb)
                {
                    return ud->CGB_SPEED | 0x7E;
                }
                else
                {
                    return 0xFF;
                }
            #endif
            }
            else if(reg == 0x4F)
            {
            #if CONFIG_FORCE_ENABLE_CGB
                if(MB_TYPE_IS_WRITE(type))
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
                if(MB_TYPE_IS_WRITE(type))
                {
                    #if CONFIG_DBG
                    _IS_DBG = 1; // Do not debug bootrom (too slow and noisy)
                    #endif
                    
                    //micache_invalidate(&ud->mb->micache);
                    micache_invalidate_range(&ud->mb->micache, 0x0000, 0x7FFF);
                    
                #if CONFIG_BOOTMEME
                    if(ud->mb->mi->dispatch_BOOTROM != NULL)
                        ud->mb->mi->dispatch_BOOTROM(userdata, 0, 0, MI_LONGDISPATCH_BOOTROM_LOCK);
                    ud->mb->mi->dispatch_BOOTROM = NULL; // disable iboot interface
                #endif
                    
                #if CONFIG_IS_CGB
                    ud->CGB_MODE |= 2; // Repurpose unwritable bit1
                #endif
                    
                    return data;
                }
                
                return 0xFF;
            }
        #if CONFIG_FORCE_ENABLE_CGB
            else if(reg == 0x51)
            {
                if(MB_TYPE_IS_WRITE(type))
                    ud->GDMA_SRC = (ud->GDMA_SRC & 0x00F0) | ((data & 0xFF) << 8);
                
                return 0xFF;
            }
            else if(reg == 0x52)
            {
                if(MB_TYPE_IS_WRITE(type))
                    ud->GDMA_SRC = (ud->GDMA_SRC & 0xFF00) | ((data & 0xF0) << 0);
                
                return 0xFF;
            }
            else if(reg == 0x53)
            {
                if(MB_TYPE_IS_WRITE(type))
                    ud->GDMA_DST = (ud->GDMA_DST & 0x00F0) | ((data & 0xFF) << 8);
                
                return 0xFF;
            }
            else if(reg == 0x54)
            {
                if(MB_TYPE_IS_WRITE(type))
                    ud->GDMA_DST = (ud->GDMA_DST & 0xFF00) | ((data & 0xF0) << 0);
                
                return 0xFF;
            }
            else if(reg == 0x55)
            {
                if(MB_TYPE_IS_WRITE(type))
                {
                    ud->GDMA_CNT = data & 0xFF;
                    
                    {
                        var count = (ud->GDMA_CNT & 0x7F) + 1;
                        
                        do
                        {
                            const r8* __restrict SRC = pgf_resolve_octant(userdata, (ud->GDMA_SRC >> 8) & 0xFF);
                            
                            if(SRC)
                            {
                                SRC += (ud->GDMA_SRC & 0xF0);
                                
                                r8* __restrict DST = &ud->mb->mi->VRAM[(ud->mb->mi->BANK_VRAM << 13) + (ud->GDMA_DST & 0x1FF0)];
                                
                                var count_sub = 0x10;
                                
                                do
                                    *(DST++) = *(SRC++);
                                while(--count_sub);
                            }
                            
                            ud->GDMA_DST += 0x10;
                            ud->GDMA_SRC += 0x10;
                        }
                        while(--count);
                        
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
            if(0) {}
        #if CONFIG_FORCE_ENABLE_CGB
            else if(reg == 0x68)
            {
                if(MB_TYPE_IS_WRITE(type))
                {
                    ud->ppu->rBGPI = data | 0x40;
                    return data;
                }
                
                return ud->ppu->rBGPI | 0x40;
            }
            else if(reg == 0x69)
            {
                var idx = ud->ppu->rBGPI & 0x3F;
                
                if(MB_TYPE_IS_WRITE(type))
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
                if(MB_TYPE_IS_WRITE(type))
                {
                    ud->ppu->rOBPI = data | 0x40;
                    return data;
                }
                
                return ud->ppu->rOBPI | 0x40;
            }
            else if(reg == 0x6B)
            {
                var idx = ud->ppu->rOBPI & 0x3F;
                
                if(MB_TYPE_IS_WRITE(type))
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
        #endif
            else if(reg == 0x70)
            {
            #if CONFIG_FORCE_ENABLE_CGB
                if(MB_TYPE_IS_WRITE(type))
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
                    if(MB_TYPE_IS_WRITE(type))
                        ud->mb->mi->BOOTROM_DATA[reg] = data;
                    else
                        return ud->mb->mi->BOOTROM_DATA[reg];
                    
                    return data;
                }
                else if(MB_TYPE_IS_WRITE(type) && ud->mb->mi->dispatch_BOOTROM != NULL)
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
    
    assert(!"bad reg");
    return 0xFF;
}

#pragma region Cartridge mapper impl

// No MBC callback
PGB_FUNC word pgf_cb_ROM_Dummy(void* userdata, word addr, word data, word type)
{
    return MB_DATA_DEFAULT;
}

PGB_FUNC word pgf_cb_ROM_MBC1(void* userdata, word addr, word data, word type)
{
    USE_UD;
    
    switch((addr >> 13) & 3)
    {
        case 0:
            break;
        case 1:
        {
            word newbank = data & 0x1F;
            if(!newbank)
                newbank = 1;
            
            ud->mb->mi->BANK_ROM = newbank & (ud->mb->mi->N_ROM - 1);
            
            micache_invalidate_range(&ud->mb->micache, 0x4000, 0x7FFF);
            break;
        }
        case 2:
            if(data && (ud->mb->mi->N_ROM > 0x20))
                assert(!"big MBC1 is not supported yet");
            break;
        case 3:
            if(ud->mb->mi->N_ROM > 0x20)
                assert(!"banking mode sel for MBC1 is not supported yet");
            break;
            
    }
    
    return MB_DATA_DEFAULT;
}

PGB_FUNC word pgf_cb_ROM_MBC2(void* userdata, word addr, word data, word type)
{
    USE_UD;
    
    if(!(addr & 0x4000))
    {
        if(addr & 0x100)
        {
            word newbank = data;
            if(!newbank)
                newbank = 1;
            
            ud->mb->mi->BANK_ROM = newbank & (ud->mb->mi->N_ROM - 1);
            
            micache_invalidate_range(&ud->mb->micache, 0x4000, 0x7FFF);
        }
    }
    
    return MB_DATA_DEFAULT;
}

PGB_FUNC word pgf_cb_ROM_MBC3(void* userdata, word addr, word data, word type)
{
    USE_UD;
    
    switch((addr >> 13) & 3)
    {
        case 0:
            break;
        case 1:
        {
            word newbank = data & 0x7F;
            if(!newbank)
                newbank = 1;
            
            ud->mb->mi->BANK_ROM = newbank & (ud->mb->mi->N_ROM - 1);
            
            micache_invalidate_range(&ud->mb->micache, 0x4000, 0x7FFF);
            break;
        }
        case 2:
            //assert(!"MBC3 RAM/reg sel is not supported yet");
            ud->mb->mi->BANK_SRAM = (data & 15) & (ud->mb->mi->N_SRAM - 1);
            micache_invalidate_range(&ud->mb->micache, 0xA000, 0xBFFF);
            break;
        case 3:
            //assert(!"MBC3 unsupported write");
            break;
            
    }
    
    return MB_DATA_DEFAULT;
}

PGB_FUNC word pgf_cb_ROM_MBC5(void* userdata, word addr, word data, word type)
{
    USE_UD;
    
    switch((addr >> 12) & 7)
    {
        case 0:
        case 1:
            break;
        case 2:
        {
            word newrom = (ud->mb->mi->BANK_ROM & ~0xFF) | (data & 0xFF);
            
            ud->mb->mi->BANK_ROM = newrom & (ud->mb->mi->N_ROM - 1);
            
            micache_invalidate_range(&ud->mb->micache, 0x4000, 0x7FFF);
            break;
        }
        case 3:
        {
            // It should be actually data&1, but
            //  who cares, it works the same due to ROM size limit
            word newrom = (ud->mb->mi->BANK_ROM & 0xFF) | ((data & 0xFF) << 8);
            
            ud->mb->mi->BANK_ROM = newrom & (ud->mb->mi->N_ROM - 1);
            
            micache_invalidate_range(&ud->mb->micache, 0x4000, 0x7FFF);
            break;
        }
        case 4:
        case 5:
            ud->mb->mi->BANK_SRAM = (data & 15) & (ud->mb->mi->N_SRAM - 1);
            micache_invalidate_range(&ud->mb->micache, 0xA000, 0xBFFF);
            break;
        default:
            //assert(!"MBC5 unsupported write");
            break;
            
    }
    
    return MB_DATA_DEFAULT;
}

static const r8 mapper_indexes[0x20] PGB_DATA =
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

static const pmiDispatch mapper_callbacks[5+1] PGB_DATA =
{
    pgf_cb_ROM_Dummy,
    pgf_cb_ROM_MBC1,
    pgf_cb_ROM_MBC2,
    pgf_cb_ROM_MBC3,
    NULL,
    pgf_cb_ROM_MBC5,
};

PGB_FUNC pmiDispatch pgf_get_mapper_callback(word mapper_id)
{
    if(mapper_id >= count_of(mapper_indexes))
        return NULL;
    
    return mapper_callbacks[mapper_indexes[mapper_id]];
}

//TODO: remove this
PGB_FUNC word pgf_cb_ROM_(void* userdata, word addr, word data, word type)
{
    USE_UD;
    
    pmiDispatch mapper_callback = pgf_get_mapper_callback(ud->mb->mi->ROM_MAPPER);
    assert(mapper_callback != NULL);
    
    return mapper_callback(userdata, addr, data, type);
}

#pragma endregion

#pragma region iboot interface

#if CONFIG_BOOTMEME
PGB_FUNC r32 pgf_cb_BOOTROM(void* userdata, r32 addr, r32 data, word type)
{
    USE_UD;
    
    const r8* __restrict ptr = (const r8* __restrict)ud->mb->mi->userdata_BOOTROM;
    
    if(type == MI_LONGDISPATCH_READ_8 && ptr != NULL)
    {
        return ptr[addr];
    }
    else if(type == MI_LONGDISPATCH_READ_32 && ptr != NULL)
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

#pragma endregion

#pragma region Default LRU handler

#if CONFIG_ENABLE_LRU
PGB_FUNC const r8* pgf_cb_ROM_LRU_(void* userdata, word addr, word bank)
{
    USE_UD;
    
    struct mi_dispatch_ROM_Bank* dispatch_ROM_Bank = (struct mi_dispatch_ROM_Bank*)ud->mb->mi->userdata_ROM_Bank;
    
    if(addr < 0x4000)
        bank = 0;
    
    var isnew = 0;
    struct lru_slot* slot = lru_get_write(dispatch_ROM_Bank->lru, addr, bank, &isnew);
    if(slot != NULL)
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
    
    return NULL;
}
#endif

#pragma endregion
