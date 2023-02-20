#include "ppu.h"


#define self ppu_t* __restrict pp
#define USE_STATE struct ppu_state_t* __restrict ps = &pp->state;

#if defined(PICOGB_PD) || defined(PICOGB_RP2)
#define IS_CGB 0
#else
#define IS_CGB (pp->is_cgb)
#endif

#if PPU_MODE == 1
#define DITHER_PATTERN (((dstX & 1) << 9) | ((dstY & 1) << 10))
#define DITHER_INDEX ((attr >> 9) & 3)
#elif PPU_MODE == 3
#define DITHER_PATTERN (((srcY & 1) << 10))
#define DITHER_INDEX ((attr >> 9) & 3)
#else
#define DITHER_PATTERN 0
#define DITHER_INDEX 0
#endif

#if PPU_IS_MONOCHROME
#define SCO_CALC ((dstX & 7) << 11)
#define SCO ((attr >> 11) & 7)
#else
#define SCO_CALC 0
#define SCO 0
#endif


#define UPDATE_0 0
#define UPDATE_1 80
#define UPDATE_2 (80 + 168)
#define UPDATE_3 456

#define crossed(tresh,prev,val) (((prev)<(tresh))&&((val)>=(tresh)))

void ppu_reset(self)
{
    USE_STATE;
    
    pp->next_update_n = 0;
    pp->next_update_ticks = 0;
    
    ps->scanX = 0;
    ps->scanY = 0;
    ps->subclk = 0;
    
    ps->posX = 0;
}

void ppu_initialize(self)
{
    pp->is_cgb = 0;
    
    pp->rLYC = 0;
    
    pp->rLCDC = 0;
    pp->rSTAT = 0x80;
    pp->rSCX = 0;
    pp->rSCY = 0;
    pp->rWX = 0;
    pp->rWY = 0;
    
    pp->rOBPI = 0;
    pp->rBGPI = 0;
    
    pp->_internal_WY = 0;
    
    #if PPU_INTERLACE == 1
    pp->state.interlace = 0b11;
    #elif PPU_INTERLACE == 2
    pp->state.interlace = 0b01;
    #else
    pp->state.interlace = ~0u;
    #endif
    
    ppu_reset(pp);
}

void ppu_turn_off(self)
{
    USE_STATE;
    
    pp->rSTAT &= 0xFC;
    pp->_internal_WY = 0;
    
    ps->scanY = 0;
    ps->scanX = 0;
    ps->subclk = 0;
}

static inline const r8* __restrict ppu_resolve_line_tile(self, word idx, word line)
{
    if(!(pp->rLCDC & (1 << 4)))
    {
        //var midx = (((~idx & 0x80) << 1) | idx) << 1;
        var midx;
        if(idx < 0x80)
            midx = (idx + 0x100);
        else
            midx = idx;
        return &pp->VRAM[(midx << 4) | (line << 1)];
        
        //0x07F 0x080  0 01111111  0 10000000
        //0x17F 0x080  1 01111111  0 10000000
        
    }
    else
        return &pp->VRAM[(idx << 4) | (line << 1)];
}

static inline const r8* __restrict ppu_resolve_line_sprite(self, word idx, word line)
{
    return &pp->VRAM[(idx << 4) | (line << 1)];
}

#if PPU_IS_DITHER
#if CONFIG_PPU_INVERT
static const pixel_t ditherbuf[4][4] =
{
    {0, 0, 0, 0},
    {1, 0, 0, 0},
    {1, 0, 0, 1},
    {1, 1, 1, 1},
};
#else
static const pixel_t ditherbuf[4][4] =
{
    {1, 1, 1, 1},
    {1, 0, 0, 1},
    {1, 0, 0, 0},
    {0, 0, 0, 0},
};
#endif
#endif

#if !PPU_IS_MONOCHROME
static const pixel_t palette[] =
#if PPU_MODE == 4
{0xA, 0x2, 0x8, 0x0};
#else
{0x202020, 0x476951, 0x79AF98, 0xBDEF86};
#endif
#endif

static void ppu_render_tile_line(self, pixel_t* __restrict scanline, const r8* __restrict tiledata, word attr)
{
    var tl = tiledata[0];
    var th = tiledata[1];
    
    var is_sprite = attr & 0x100;
    
    #if PPU_IS_MONOCHROME
    #if PPU_IS_DITHER
    var spos = DITHER_INDEX;
    #endif
    var ppos = SCO;
    
    var pm = 0;
    #endif
    
    var px = 0;
    
    var j = 8;
    for(;;)
    {
        --j;
        
        var bs = !(attr & (1 << 5)) ? j : 7 - j;
        
        var bd = ((tl >> bs) & 1) | (((th >> bs) & 1) << 1);
        if(is_sprite && !bd)
        {
            #if PPU_IS_MONOCHROME
            px <<= PPU_IS_BPP;
            pm <<= PPU_IS_BPP;
            #else
            ++scanline;
            #endif
            if(j)
                continue;
            
            break;
        }
        
        if(IS_CGB)
        {
            var coli = ((attr & 7) << 2) | bd;
            
            //var px = pp->BGP[coli];
            
            #if !CONFIG_PPU_CGB_MONO
            var px16 = !is_sprite ? pp->BGP[coli] : pp->OBP[coli];
            
            var comp = (px16 & 0x1F) << 3;
            comp |= comp >> 5;
            px = comp << 16;
            
            comp = ((px16 >> 5) & 0x1F) << 3;
            comp |= comp >> 5;
            px |= comp << 8;
            
            comp = ((px16 >> 10) & 0x1F) << 3;
            comp |= comp >> 5;
            px |= comp << 0;
            #else
            px = palette
        #if CONFIG_PPU_INVERT
            [bd^3];
        #else
            [bd];
        #endif
            #endif
        }
        else
        {
            if(!is_sprite)
            {
                bd = (pp->rBGP >> (bd + bd)) & 3;
            }
            else
            {
                if(!(attr & 0x10))
                    bd = (pp->rOBP0 >> (bd + bd)) & 3;
                else
                    bd = (pp->rOBP1 >> (bd + bd)) & 3;
            }
            
            
            #if PPU_IS_MONOCHROME
            
            //px = (px << 1) | (bd >> 1);
            
            #if PPU_IS_DITHER
            px = (px << PPU_IS_BPP) | ditherbuf[bd][spos ^ (j & ((1 << PPU_IS_BPP) - 1))];
            #else
            px = (px << PPU_IS_BPP) | (bd >> (2 - PPU_IS_BPP));
            #endif
            pm = (pm << PPU_IS_BPP) | ((1 << PPU_IS_BPP) - 1);
            
            #else
            
            px = palette
        #if CONFIG_PPU_INVERT
            [bd^3];
        #else
            [bd];
        #endif
            
            #if PPU_MODE == 4
            px |= px << 4;
            #endif
            
            #endif
            /*
            if(!(coli >> 2))
                px = 0xFFFFFF;
            else
            {
                px = 0;
                if(coli & (1 << 2))
                    px |= 0x000000FF;
                if(coli & (1 << 3))
                    px |= 0x0000FF00;
                if(coli & (1 << 4))
                    px |= 0x00FF0000;
            }
            
            switch(coli & 3)
            {
                default:
                    break;
                case 2:
                    px = (px >> 1) & 0x7F7F7F7F;
                    break;
                case 1:
                    px = (px >> 2) & 0x3F3F3F3F;
                    break;
                case 0:
                    px = (px >> 3) & 0x1F1F1F1F;
                    break;
            }
            */
        }
        
        #if !PPU_IS_MONOCHROME
        *(scanline++) = px;
        #endif
        
        if(!j)
            break;
    }
    
    #if PPU_IS_MONOCHROME
    if(ppos)
    {
        // 
        
        *(scanline) = (*(scanline) & (~pm >> ppos)) | ((px & pm) >> ppos);
        
        ++scanline;
        
        *(scanline) = (*(scanline) & (~(pm << (8 - ppos)))) | ((px & pm) << (8 - ppos));
    }
    else if(pm == ((1 << (PPU_IS_BPP * PPU_IS_IPP)) - 1))
    {
        *(scanline) = px;
    }
    else
    {
        *(scanline) = (*(scanline) & ~pm) | (px & pm);
    }
    #endif
}

static void ppu_render_scanline(self)
{
    USE_STATE;
    
    var dstY = ps->scanY;
    var srcY = (dstY + pp->rSCY) & 0xFF;
    var srcX = pp->rSCX;
    var dstX = 8 - (srcX & 7);
    
    const r8* __restrict r_line_idx = &pp->VRAM[0x1800 | ((srcY >> 3) << 5) | ((pp->rLCDC & (1 << 3)) << 7)];
    
    pixel_t* __restrict scanline = ps->framebuffer[dstY];
    
    var i;
    
    srcX >>= 3;
    srcY &= 7;
    
    if(pp->rLCDC & 1)
    {
        var pattern = DITHER_PATTERN | SCO_CALC;
        
        for(i = 0; i != 21; ++i)
        {
            var idx = r_line_idx[srcX];
            var attr = IS_CGB ? r_line_idx[srcX + 0x2000] : 0;
            
            var tileY = !(attr & (1 << 6)) ? srcY : 7 - srcY;
            
            const r8* __restrict tiledata = ppu_resolve_line_tile(pp, idx, tileY);
            if(attr & (1 << 3))
                tiledata += 0x2000;
            
            ppu_render_tile_line(pp, &scanline[dstX / PPU_IS_IPP], tiledata, attr | pattern);
            
            dstX += 8;
            
            srcX = (srcX + 1) & 31;
        }
    }
    
    if((pp->rLCDC & 0x20) && (pp->rWY <= dstY) && (pp->rWX < 168) && pp->rWX)
    //if(0)
    {
        srcY = pp->_internal_WY;
        srcX = 0;
        dstX = pp->rWX + 1;
        
        var pattern = DITHER_PATTERN | SCO_CALC;
        
        
        r_line_idx = &pp->VRAM[0x1800 | ((srcY >> 3) << 5) | ((pp->rLCDC & (1 << 6)) << 4)];
        
        srcY &= 7;
        while(dstX < 168)
        {
            var idx = r_line_idx[srcX];
            
            const r8* __restrict tiledata = ppu_resolve_line_tile(pp, idx, srcY);
            ppu_render_tile_line(pp, &scanline[dstX / PPU_IS_IPP], tiledata, pattern);
            
            dstX += 8;
            
            srcX = (srcX + 1) & 31;
        }
    }
    
    if(pp->rLCDC & 2)
    {
        var pattern = 0x100;
        
        for(i = 0; i != 10; ++i)
        {
            var posX, idx;
            {
                hilow16_t hilo = pp->state.latches[i];
                posX = hilo.low;
                idx = hilo.high;
            }
            if(idx >= 40 || posX >= 168 || !posX)
                continue;
            
            const r8* __restrict oamslice = &pp->OAM[idx << 2];
            
            var y = oamslice[0];
            var offy = dstY + 16 - y;
            if(offy >> 4)
                continue;
            
            var tile = oamslice[2];
            var attr = oamslice[3];
            
            if(!(pp->rLCDC & (1 << 2)))
            {
                if(attr & (1 << 6))
                    offy = 7 - offy;
            }
            else
            {
                tile &= 0xFE;
                
                if(attr & (1 << 6))
                    offy = 15 - offy;
            }
            
            dstX = posX;
            
            const r8* __restrict tiledata = ppu_resolve_line_sprite(pp, tile, offy);
            
            ppu_render_tile_line(pp, &scanline[dstX / PPU_IS_IPP], tiledata, attr | pattern | SCO_CALC | DITHER_PATTERN);
        }
    }
}

static inline word ppu_stat_set_mode(self, word mode)
{
    var prev = pp->rSTAT & 3;
    pp->rSTAT = (pp->rSTAT & 0xFC) | (mode & 3);
    return prev;
}

//#include <stdio.h>

static void ppu_update_LYC(self, word scanY)
{
    if(scanY == pp->rLYC)
    {
        //printf("- ?LYCC %02X\n", scanY);
        
        if(!(pp->rSTAT & 4) && (pp->rSTAT & (1 << 6)))
        {
            //puts("  - Setting LYC IRQ!");
            pp->IF_SCHED |= 2;
        }
        
        pp->rSTAT |= 4;
    }
    else
    {
        pp->rSTAT &= 0xFB;
    }
}

static void ppu_update_newline(self, word scanY)
{
    ppu_update_LYC(pp, scanY);
    
    if(scanY < 144)
    {
        ppu_stat_set_mode(pp, 2);
        
        if(pp->rSTAT & (1 << 5))
            pp->IF_SCHED |= 2;
    }
    else
    {
        if(ppu_stat_set_mode(pp, 1) != 1)
        {
            if(pp->rSTAT & (1 << 4))
                pp->IF_SCHED |= 2;
        }
    }
    
    if(scanY == 144)
    {
        pp->IF_SCHED |= 1;
        
        pp->_internal_WY = 0;
        
    #if !PPU_SCANLINE_UPDATES
        pp->_redrawed = 1;
    #endif
    }
}

void ppu_turn_on(self)
{
    ppu_update_newline(pp, 0);
}


static inline void ppu_tick_internal_1(self)
{
    USE_STATE;
    
    ppu_stat_set_mode(pp, 3);
    
    var scanY = ps->scanY;
    var i, j = 0;
    
    if(pp->rLCDC & 2)
    {
        var tresh = scanY + 16;
        var tresl = tresh - (8 << ((pp->rLCDC >> 2) & 1));
        
        for(i = 0; i != 40; ++i)
        {
            const r8* __restrict oamslice = &pp->OAM[i << 2];
            
            var y = oamslice[0];
            var x = oamslice[1];
            
            if(tresl < y && tresh >= y)
            {
                ps->latches[j].low = x < 168 ? x : 0;
                ps->latches[j].high = i;
                
                if(++j == 10)
                    break;
            }
        }
    }
    
    while(j != 10)
        ps->latches[j++].low = 0;
}

static inline void ppu_tick_internal_2(self)
{
    USE_STATE;
    
    ppu_stat_set_mode(pp, 0);
    
    var scanY = ps->scanY;
    
    if(pp->rSTAT & (1 << 3))
        pp->IF_SCHED |= 2;
    
    #if PPU_INTERLACE != 0
    if(scanY & 1)
    {
        if(ps->interlace & 2)
            ppu_render_scanline(pp);
    }
    else
    {
        if(ps->interlace & 1)
            ppu_render_scanline(pp);
    }
    #else
    ppu_render_scanline(pp);
    #endif
    
    if((pp->rLCDC & 0x20) && (pp->rWY <= scanY) && (pp->rWX < 168) && pp->rWX)
        ++(pp->_internal_WY);
    
    
#if PPU_SCANLINE_UPDATES
    pp->_redrawed = 1;
#endif
}

static inline void ppu_tick_internal_3(self)
{
    USE_STATE;
    
    var scanY = ps->scanY;
    
    if((++scanY) == 154)
    {
        ps->interlace = ~ps->interlace;
        
        scanY = 0;
    }
    
    ps->scanY = scanY;
    
    ppu_update_newline(pp, scanY);
}

/*
static void ppu_tick_internal(self, word ncycles)
{
    USE_STATE;
    
    var scanX = ps->scanX;
    var endcycle = scanX + ncycles;
    
    var scanY = ps->scanY;
    
    var i, j;
    
    if(scanY >= 144)
        goto ppu_end;
    
    if(crossed(UPDATE_1, scanX, endcycle))
        ppu_tick_internal_1(pp);
    
    if(crossed(UPDATE_2, scanX, endcycle))
        ppu_tick_internal_2(pp);
    
    ppu_end:
    if(endcycle >= UPDATE_3)
    {
        
        
        //ppu_update_LYC(pp, scanY);
    }
    ps->scanX = endcycle;
}*/

void ppu_tick_internal(self, word ncycles, word rem)
{
    var ren = pp->next_update_n;
    
    for(;;)
    {
        ncycles -= rem;
        
        switch(ren)
        {
            case 0: // Start of line / OAM scan
            default:
                ppu_update_newline(pp, pp->state.scanY);
                
                if(pp->state.scanY < 144)
                {
                    rem = UPDATE_1 - UPDATE_0;
                    ren = 1;
                }
                else
                {
                    rem = UPDATE_3 - UPDATE_0;
                    ren = 3;
                }
                
                break;
            
            case 1: // Displaying
                ppu_tick_internal_1(pp);
                
                rem = UPDATE_2 - UPDATE_1;
                ren = 2;
                break;
            case 2: // HBlank
                ppu_tick_internal_2(pp);
                
                rem = UPDATE_3 - UPDATE_2;
                ren = 3;
                break;
            case 3: // End of line
                ppu_tick_internal_3(pp);
                
                ren = 0;
                rem = 0;
                break;
        }
        
        if(ncycles < rem) // Not enough cycles to spend to move into a new mode
        {
            pp->next_update_ticks = rem - ncycles;
            pp->next_update_n = ren;
            return;
        }
    }
    
    /*
    if(endcycle >= UPDATE_3)
    {
        endcycle = ncycles;
        do
        {
            var sub = UPDATE_3 - scanX;
            ppu_tick_internal(pp, sub);
            scanX = 0;
            endcycle -= sub;
        }
        while(endcycle >= UPDATE_3);
        
        if(endcycle)
            ppu_tick_internal(pp, endcycle);
    }
    else
    {
        ppu_tick_internal(pp, ncycles);
    }*/
}
