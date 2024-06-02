#include "ppu.h"

#include <assert.h>

#define self ppu_t* __restrict
#define USE_STATE struct ppu_state_t* __restrict ps = &pp->state;

//#define PPU_DEBUG


#if !CONFIG_IS_CGB
#define IS_CGB 0
#elif CONFIG_FORCE_ENABLE_CGB
#define IS_CGB 1
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

#ifdef PPU_DEBUG
#define RENDER_WIDTH 240
#else
#define RENDER_WIDTH 168
#endif


#define UPDATE_0 0
#define UPDATE_1 80
#define UPDATE_2 (80 + 168)
#define UPDATE_3 456

#define crossed(tresh,prev,val) (((prev)<(tresh))&&((val)>=(tresh)))

PGB_FUNC void ppu_reset(self pp)
{
    USE_STATE;
    
    pp->next_update_n = 3;
    pp->next_update_ticks = 0;
    
    ps->scanX = 0;
    ps->scanY = 0;
    ps->subclk = 0;
    
    ps->posX = 0;
    
    pp->_internal_WY = 0;
    
    pp->rSTAT = (pp->rSTAT & 0xFC) | 2;
}

PGB_FUNC void ppu_initialize(self pp)
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
    
    #if PPU_INTERLACE == 1
    pp->state.interlace = 0b11;
    #elif PPU_INTERLACE == 2
    pp->state.interlace = 0b01;
    #else
    pp->state.interlace = ~0u;
    #endif
    
    ppu_reset(pp);
}

PGB_FUNC void ppu_turn_off(self pp)
{
    USE_STATE;
    
    pp->rSTAT &= 0xFC;
    pp->_internal_WY = 0;
    
    ps->scanY = 0;
    ps->scanX = 0;
    ps->subclk = 0;
}

PGB_FUNC static inline __attribute__((optimize("O2"))) const r8* __restrict ppu_resolve_line_tile(const self pp, word idx, word line)
{
    const r8* __restrict ret;
    var LCDC;
    var magic;
    
    // each 8px tile line is two bytes
    line <<= 1;
    COMPILER_VARIABLE_BARRIER(line);
    
    // offset VRAM by selected tile line
    ret = &pp->VRAM[line];
    COMPILER_VARIABLE_BARRIER(ret);
    
    // LCDC = (LCDC & (1 << 4)) ? 0 : (1 << 8);
    // The addition works as XOR, as only that single bit is needed
    LCDC = (pp->rLCDC + (1 << 4)) << (31 - 4) >> 31 << 8;
    COMPILER_VARIABLE_BARRIER(LCDC);
    
    // magic = idx.7 NOR LCDC.4
    // magic = (idx & 0x80) ? 0 : (1 << 8) | garbage;
    // Since LCDC here is only a single bit, it will clear the other garbage
    magic = (idx - (1 << 7));
    magic &= LCDC;
    
    // Add in real index
    magic |= idx;
    
    // Each tile is 16bytes long, offset VRAM by real tile index
    return ret + (magic << 4);
    
    // The non-obfuscated version:
    
    /*
    if(!(pp->rLCDC & (1 << 4)))
    {
        var midx;
        if(idx < 0x80)
            midx = (idx + 0x100);
        else
            midx = idx;
        return &pp->VRAM[(midx << 4) | (line << 1)];
    }
    else
        return &pp->VRAM[(idx << 4) | (line << 1)];
    */
}

PGB_FUNC static inline const r8* __restrict ppu_resolve_line_sprite(const self pp, word idx, word line)
{
    return &pp->VRAM[(idx << 4) | (line << 1)];
}

#if CONFIG_IS_CGB
PGB_FUNC static inline const r8* __restrict ppu_resolve_line_sprite_cgb1(const self pp, word idx, word line)
{
    return &pp->VRAM[((idx << 4) | (line << 1)) | 0x2000];
}
#endif

#if PPU_MODE == 0
#include "ppu/ppu.RGBA8.c"
#elif PPU_MODE == 1 || PPU_MODE == 2 || PPU_MODE == 3
#include "ppu/ppu.1bpp.c"
#elif PPU_MODE == 4
#include "ppu/ppu.RGBI_44.c"
#elif PPU_MODE == 5
#include "ppu/ppu.RGBI_8.c"
#elif PPU_MODE == 6
#include "ppu/ppu.RGB565.c"
#else
#error Undefined PPU mode
#endif

PGB_FUNC static void ppu_render_tile_line(self pp, pixel_t* __restrict scanline, const r8* __restrict tiledata, word attr)
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
        
    #if CONFIG_IS_CGB
        if(IS_CGB)
        {
            var coli = ((attr & 7) << 2) | bd;
            
            #if !CONFIG_PPU_CGB_MONO
            var px16 = !is_sprite ? pp->BGP[coli] : pp->OBP[coli];
            
        #if PPU_MODE != 6
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
            px = px16;
        #endif
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
    #endif
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
            
        #if CONFIG_PPU_INVERT
            bd ^= 3;
        #endif
            
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

PGB_FUNC static void ppu_render_scanline(self pp)
{
    USE_STATE;
    
    var dstY = ps->scanY;
    var srcY = (dstY + pp->rSCY) & 0xFF;
    var srcX = pp->rSCX;
    var dstX = 8 - (srcX & 7);
    
    const r8* __restrict r_line_idx = &pp->VRAM[0x1800 | ((srcY >> 3) << 5) | ((pp->rLCDC & (1 << 3)) << 7)];
    
    pixel_t* __restrict scanline = ps->framebuffer[dstY];
    
    const var window_enable = (pp->rLCDC & 0x20) && (pp->rWY <= dstY) && (pp->rWX < RENDER_WIDTH);
    
    var i;
    
    srcX >>= 3;
    srcY &= 7;
    
#if !CONFIG_IS_CGB
    if(pp->rLCDC & 1)
#endif
    {
        var pattern = DITHER_PATTERN | SCO_CALC;
        
        var maxsize;
        if(!window_enable)
            maxsize = ((RENDER_WIDTH + 7) >> 3);
        else
            maxsize = (pp->rWX + 7) >> 3;
        
        for(i = 0; i < maxsize; ++i)
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
    
    if(window_enable)
    //if(0)
    {
        srcY = pp->_internal_WY;
        srcX = 0;
        dstX = pp->rWX + 1;
        
        var pattern = DITHER_PATTERN | SCO_CALC;
        
        
        r_line_idx = &pp->VRAM[0x1800 | ((srcY >> 3) << 5) | ((pp->rLCDC & (1 << 6)) << 4)];
        
        srcY &= 7;
        while(dstX < RENDER_WIDTH)
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
        
        i = PPU_N_OBJS;
        while(i)
        {
            --i;
            
            var posX, idx;
            {
                hilow16_t hilo = pp->state.latches[i];
                posX = hilo.low;
                idx = hilo.high;
            }
            if(!posX || posX >= RENDER_WIDTH || idx >= 40)
                continue;
            
            const r8* __restrict oamslice = &pp->OAM[idx << 2];
            
            var y = oamslice[0];
            var objLine = dstY + 16 - y;
            
            var tile = oamslice[2];
            var attr = oamslice[3];
            
            if(!(pp->rLCDC & (1 << 2)))
            {
                if(objLine >= 8)
                    continue;
                
                if(attr & (1 << 6))
                    objLine = 7 - objLine;
            }
            else
            {
                if(objLine >= 16)
                    continue;
                
                tile &= 0xFE;
                
                if(attr & (1 << 6))
                    objLine = 15 - objLine;
            }
            
            objLine &= 15;
            dstX = posX;
            
            const r8* __restrict tiledata;
            
        #if CONFIG_IS_CGB
            if(!IS_CGB || !(attr & (1 << 3)))
        #endif
                tiledata = ppu_resolve_line_sprite(pp, tile, objLine);
        #if CONFIG_IS_CGB
            else
                tiledata = ppu_resolve_line_sprite_cgb1(pp, tile, objLine);
        #endif
            
            ppu_render_tile_line(pp, &scanline[dstX / PPU_IS_IPP], tiledata, attr | pattern | SCO_CALC | DITHER_PATTERN);
        }
    }
}

PGB_FUNC static inline word ppu_stat_set_mode(self pp, word mode)
{
    var prev = pp->rSTAT & 3;
    pp->rSTAT = (pp->rSTAT & 0xFC) | (mode & 3);
    return prev;
}

PGB_FUNC static void ppu_update_LYC(self pp, word scanY)
{
    if(scanY == pp->rLYC)
    {
        if(!(pp->rSTAT & 4) && (pp->rSTAT & (1 << 6)))
            pp->IF_SCHED |= 2;
        
        pp->rSTAT |= 4;
    }
    else
    {
        pp->rSTAT &= 0xFB;
    }
}

PGB_FUNC static void ppu_update_newline(self pp, word scanY)
{
    ppu_update_LYC(pp, scanY);
    
    if(scanY < 144)
    {
        ppu_stat_set_mode(pp, 2);
        
        if(pp->rSTAT & (1 << 5))
            pp->IF_SCHED |= 2;
    }
    else if(ppu_stat_set_mode(pp, 1) != 1)
    {
        if(pp->rSTAT & (3 << 4))
            pp->IF_SCHED |= 2;
        
        pp->IF_SCHED |= 1;
        
        pp->_internal_WY = 0;
        
        pp->_redrawed = 255;
    }
}

PGB_FUNC void ppu_turn_on(self pp)
{
    ppu_reset(pp);
    ppu_update_LYC(pp, pp->state.scanY);
}

PGB_FUNC void ppu_on_write_LYC(self pp)
{
    ppu_update_LYC(pp, pp->state.scanY);
}

PGB_FUNC static inline void ppu_trigger_oam_scan(self pp)
{
    USE_STATE;
    
    var scanY = ps->scanY;
    var i, j = 0;
    
#if !CONFIG_IS_CGB
    if(pp->rLCDC & 2)
#endif
    {
        var tresh = scanY + 16;
        var tresl;
        if(pp->rLCDC & 4)
            tresl = tresh - 16;
        else
            tresl = tresh - 8;
            
        
        for(i = 0; i != 40; ++i)
        {
            const r8* __restrict oamslice = &pp->OAM[i << 2];
            
            var y = oamslice[0];
            var x = oamslice[1];
            
            if(tresl < y && tresh >= y)
            {
                ps->latches[j].low = x < 168 ? x : 0;
                ps->latches[j].high = i;
                
                if(++j == PPU_N_OBJS)
                    break;
            }
        }
    }
    
    while(j != PPU_N_OBJS)
        ps->latches[j++].low = 0;
}

PGB_FUNC static inline void ppu_trigger_scanline_draw(self pp)
{
    USE_STATE;
    
    var scanY = ps->scanY;
    
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
    
#if PPU_SCANLINE_UPDATES
    pp->_redrawed = scanY + 1;
#endif
    
    if((pp->rLCDC & 0x20) && (pp->rWY <= scanY) && (pp->rWX < 168) && pp->rWX)
        ++(pp->_internal_WY);
}

PGB_FUNC static inline word ppu_tick_internal_0(self pp)
{
    var scanY = pp->state.scanY;
    
    ppu_update_newline(pp, scanY);
    
#if CONFIG_PPU_ACTION_ON_START
    ppu_trigger_oam_scan(pp);
#endif
    
    return scanY < 144;
}

PGB_FUNC static inline void ppu_tick_internal_1(self pp)
{
    ppu_stat_set_mode(pp, 3);
    
#if !CONFIG_PPU_ACTION_ON_START
    ppu_trigger_oam_scan(pp);
#else
    ppu_trigger_scanline_draw(pp);
#endif
}

PGB_FUNC static inline void ppu_tick_internal_2(self pp)
{
    ppu_stat_set_mode(pp, 0);
    
    if(pp->rSTAT & (1 << 3))
        pp->IF_SCHED |= 2;
    
#if !CONFIG_PPU_ACTION_ON_START
    ppu_trigger_scanline_draw(pp);
#endif
}

PGB_FUNC static inline void ppu_tick_internal_3(self pp)
{
    USE_STATE;
    
    var scanY = ps->scanY;
    
    if((++scanY) == 154)
    {
    #if PPU_INTERLACE != 0
        ps->interlace = ~ps->interlace;
    #endif
        
        scanY = 0;
    }
    
    ps->scanY = scanY;
}

PGB_FUNC void ppu_tick_internal(self pp, word ncycles, word rem)
{
    var ren = pp->next_update_n;
    
    for(;;)
    {
        ncycles -= rem;
        
        switch(ren)
        {
            case 0: // Start of line / OAM scan
            default:
                if(ppu_tick_internal_0(pp))
                {
                    rem = UPDATE_1 - UPDATE_0;
                    ren = 1;
                }
                else if(pp->state.scanY != 153)
                {
                    rem = UPDATE_3 - UPDATE_0;
                    ren = 3;
                }
                else // WTF
                {
                    rem = 4;
                    ren = 3;
                }
                
                ppu_update_LYC(pp, pp->state.scanY);
                
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
                if(pp->state.scanY != 153)
                {
                    ren = 0;
                    rem = 0;
                }
                else
                {
                    ren = 0;
                    rem = 456 - 4; // ???
                }
                
                ppu_tick_internal_3(pp);
                break;
        }
        
        if(ncycles < rem) // Not enough cycles to spend to move into a new mode
        {
            pp->next_update_ticks = rem - ncycles;
            pp->next_update_n = ren;
            return;
        }
    }
}
