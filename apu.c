#include "apu.h"



void apu_reset(apu_t* __restrict pp);
void apu_initialize(apu_t* __restrict pp);


//#define APU_DIV_512Hz (0x3FF & ~(APU_N_PER_TICK - 1))
//#define APU_DIV_256Hz (0x7FF & ~(APU_N_PER_TICK - 1))
//#define APU_DIV_128Hz (0xFFF & ~(APU_N_PER_TICK - 1))
//#define APU_DIV_64Hz  (0x1FFF & ~(APU_N_PER_TICK - 1))

#define APU_DIV_512Hz (0x3FF & ~(APU_N_PER_TICK - 1))
#define APU_DIV_256Hz (0x7FF & ~(APU_N_PER_TICK - 1))
#define APU_DIV_128Hz (0xFFF & ~(APU_N_PER_TICK - 1))
#define APU_DIV_64Hz  (0x1FFF & ~(APU_N_PER_TICK - 1))

#define APU_BIAS 8


static const r8 patterns[4] PGB_DATA =
{
    0b11111110,
    0b01111110,
    0b01111000,
    0b10000001
};

#pragma region Helper functions

static inline void apuchi_clear_trigger(struct apu_ch_t* __restrict ch)
{
    ch->NR_RAW[4] = ch->NR_RAW[4] & 0x7F;
}

static inline wbool apuchi_is_trigger(const struct apu_ch_t* __restrict ch)
{
    return ch->NR_RAW[4] & 0x80;
}

static inline word apuchi_get_reload(const struct apu_ch_t* __restrict ch)
{
    return (~(ch->NR_RAW[3] | (ch->NR_RAW[4] << 8))) & 0x7FF;
}

static inline word apuchi_get_ch1_sample_type(const struct apu_ch_t* __restrict ch)
{
    return (ch->NR_RAW[1] >> 6) & 3;
}

static inline word apuchi_get_ch_length(const struct apu_ch_t* __restrict ch)
{
    return (~(ch->NR_RAW[1])) & 0x3F;
}

static inline word apuchi_get_ch3_length(const struct apu_ch_t* __restrict ch)
{
    return (~(ch->NR_RAW[1])) & 0xFF;
}

static inline word apuchi_get_volsweep_ctr(const struct apu_ch_t* __restrict ch)
{
    return ch->NR_RAW[2] & 7;
}

static inline wbool apuchi_get_volsweep_is_increase(const struct apu_ch_t* __restrict ch)
{
    return (ch->NR_RAW[2] >> 3) & 1;
}

static inline word apuchi_get_volsweep_volume(const struct apu_ch_t* __restrict ch)
{
    return (ch->NR_RAW[2] >> 4) & 0xF;
}

static inline wbool apuchi_get_dac_ch(const struct apu_ch_t* __restrict ch)
{
    return ch->NR_RAW[2] & 0xF8;
}

static inline wbool apuchi_get_dac_ch3(const struct apu_ch_t* __restrict ch)
{
    return ch->NR_RAW[0] & 0x80;
}

#pragma endregion

#pragma region Collapsible misc stuff

void apu_reset(apu_t* __restrict pp)
{
    word i;
    
    for(i = 0; i < 4; i++)
    {
        pp->ch[i].vol = 0;
        pp->ch[i].sample_no = 0;
        pp->ch[i].is_on = 0;
        
        pp->ch[i].NR_RAW[0] = 0;
        pp->ch[i].NR_RAW[1] = 0;
        pp->ch[i].NR_RAW[2] = 0;
        pp->ch[i].NR_RAW[3] = 0;
        pp->ch[i].NR_RAW[4] = 0;
        
        
    }
    
    pp->MASTER_CFG &= 0xFF700000;
    
    pp->CTR_INT = 0;
    pp->CTR_INT_FRAC = 0;
}

void apu_initialize(apu_t* __restrict pp)
{
    apu_reset(pp);
    
    pp->outbuf_pos = 0;
    pp->CTR_DIV = 0;
    pp->CTR_INT = 0;
    pp->CTR_INT_FRAC = 0;
    pp->MASTER_CFG = 0;
}

static void apu_init_ch3(apu_t* __restrict pp, word _ch)
{
    struct apu_ch_t* __restrict ch = &pp->ch[_ch];
    
    if(apuchi_is_trigger(ch))
    {
        apuchi_clear_trigger(ch);
        
        if(!(ch->NR_RAW[0] & 0x80))
            return;
        
        pp->MASTER_CFG |= 1 << (_ch + 24);
        
        ch->vol = 1;
        ch->sweep_ctr = 0;
        ch->ctr = 0;
        
        ch->sample_no = 0;
        
        ch->length_ctr = apuchi_get_ch3_length(ch);
        
        ch->is_on = 1;
    }
}

static void apu_init_ch(apu_t* __restrict pp, word _ch)
{
    struct apu_ch_t* __restrict ch = &pp->ch[_ch];
    
    if(apuchi_is_trigger(ch))
    {
        apuchi_clear_trigger(ch);
        
        if(!apuchi_get_dac_ch(ch))
            return;
        
        ch->vol = apuchi_get_volsweep_volume(ch);
        
        pp->MASTER_CFG |= 1 << (_ch + 24);
        
        ch->ctr = 0;
        ch->sample_no = 0;
        
        if(_ch == 3)
        {
            ch->sample_no = 1;
            ch->raw = 0;
        }
        
        ch->length_ctr = apuchi_get_ch_length(ch);
        ch->sweep_ctr = apuchi_get_volsweep_ctr(ch);
        
        ch->is_on = 1;
    }
}

//#include <stdio.h>

void apu_write(apu_t* __restrict pp, word addr, word data)
{
    addr = (addr - 0x10) & 0x1F;
    
    //if(addr >= (0 * 5) && addr < (1 * 5))
    //printf("NR%u%u = %02X\n", (addr / 5) + 1, addr % 5, data);
    
    if(addr >= 22)
        ;
    else if((pp->MASTER_CFG & (1 << 23)))
        ;
    else
        return;
    
    switch(addr)
    {
        case 0: pp->ch[0].NR_RAW[0] = data;
        {
            if(!(data & (7 << 4)))
                pp->ch[0].raw = 0;
            
            break;
        }
        case 1: pp->ch[0].NR_RAW[1] = data; break;
        case 2: pp->ch[0].NR_RAW[2] = data; if(!(data & 0xF8)) pp->ch[0].is_on = 0; break;
        case 3: pp->ch[0].NR_RAW[3] = data; break;
        case 4: pp->ch[0].NR_RAW[4] = data; goto apu_init_ch1_lbl;
        apu_init_ch1_lbl:
        {
            apu_init_ch(pp, 0);
            break;
        }
        
        case 5: break;
        case 6: pp->ch[1].NR_RAW[1] = data; break;
        case 7: pp->ch[1].NR_RAW[2] = data; if(!(data & 0xF8)) pp->ch[1].is_on = 0; break;
        case 8: pp->ch[1].NR_RAW[3] = data; break;
        case 9: pp->ch[1].NR_RAW[4] = data; goto apu_init_ch2_lbl;
        apu_init_ch2_lbl:
        {
            apu_init_ch(pp, 1);
            break;
        }
        
        case 10: pp->ch[2].NR_RAW[0] = data; if(!(data & 0x80)) pp->ch[2].is_on = 0; break;
        case 11: pp->ch[2].NR_RAW[1] = data; break;
        case 12: pp->ch[2].NR_RAW[2] = data; break;
        case 13: pp->ch[2].NR_RAW[3] = data; break;
        case 14: pp->ch[2].NR_RAW[4] = data; goto apu_init_ch3_lbl;
        apu_init_ch3_lbl:
        {
            apu_init_ch3(pp, 2);
            break;
        }
        
        case 15: break;
        case 16: pp->ch[3].NR_RAW[1] = data; break;
        case 17: pp->ch[3].NR_RAW[2] = data; if(!(data & 0xF8)) pp->ch[3].is_on = 0; break;
        case 18: pp->ch[3].NR_RAW[3] = data; break;
        case 19: pp->ch[3].NR_RAW[4] = data; goto apu_init_ch4_lbl;
        apu_init_ch4_lbl:
        {
            apu_init_ch(pp, 3);
            break;
        }
        
        case 20:
            pp->MASTER_CFG = (pp->MASTER_CFG & 0xFFFFFF00) | ((data & 0xFF) << 0);
            break;
        case 21:
            pp->MASTER_CFG = (pp->MASTER_CFG & 0xFFFF00FF) | ((data & 0xFF) << 8);
            break;
        
        case 22:
            if(!(data & 0x80) && (pp->MASTER_CFG & (1 << 23)))
            {
                apu_reset(pp);
            }
            
            pp->MASTER_CFG = (pp->MASTER_CFG & 0xFF7FFFFF) | ((data & 0x80) << 16);
            break;
        
        default:
            __builtin_unreachable();
            return;
    }
}

word apu_read_internal(apu_t* __restrict pp, word addr)
{
    switch(addr)
    {
        case 0: return pp->ch[0].NR_RAW[0];
        case 1: return pp->ch[0].NR_RAW[1];
        case 2: return pp->ch[0].NR_RAW[2];
        case 3: return pp->ch[0].NR_RAW[3];
        case 4: return pp->ch[0].NR_RAW[4];
        
        case 5: return pp->ch[1].NR_RAW[0];
        case 6: return pp->ch[1].NR_RAW[1];
        case 7: return pp->ch[1].NR_RAW[2];
        case 8: return pp->ch[1].NR_RAW[3];
        case 9: return pp->ch[1].NR_RAW[4];
        
        case 10: return pp->ch[2].NR_RAW[0];
        case 11: return pp->ch[2].NR_RAW[1];
        case 12: return pp->ch[2].NR_RAW[2];
        case 13: return pp->ch[2].NR_RAW[3];
        case 14: return pp->ch[2].NR_RAW[4];
        
        case 15: return pp->ch[3].NR_RAW[0];
        case 16: return pp->ch[3].NR_RAW[1];
        case 17: return pp->ch[3].NR_RAW[2];
        case 18: return pp->ch[3].NR_RAW[3];
        case 19: return pp->ch[3].NR_RAW[4];
            
        case 20:
            return (pp->MASTER_CFG >> 0) & 0xFF;
        case 21:
            return (pp->MASTER_CFG >> 8) & 0xFF;
        
        case 22:
            if(!(pp->ch[0].vol)) pp->MASTER_CFG &= 0xFFFEFFFF;
            if(!(pp->ch[1].vol)) pp->MASTER_CFG &= 0xFFFDFFFF;
            if(!(pp->ch[2].vol)) pp->MASTER_CFG &= 0xFFFBFFFF;
            if(!(pp->ch[3].vol)) pp->MASTER_CFG &= 0xFFF7FFFF;
            
            return ((pp->MASTER_CFG >> 16) & 0x8F) | 0x70;
        
        default:
            __builtin_unreachable();
            return 0xFF;
    }
}

static const r8 APU_BITS[23] PGB_DATA =
{
    0x80, 0x3F, 0x00, 0xFF, 0xBF,
    0xFF, 0x3F, 0x00, 0xFF, 0xBF,
    0x7F, 0xFF, 0x9F, 0xFF, 0xBF,
    0xFF, 0xFF, 0x00, 0x00, 0xBF,
    0x00, 0x00, 0x70
};

word apu_read(apu_t* __restrict pp, word addr)
{
    addr = (addr - 0x10) & 0x1F;
    
    return apu_read_internal(pp, addr) | APU_BITS[addr];
}

void apu_write_wave(apu_t* __restrict pp, word addr, word data)
{
    pp->WVRAM[addr & 0xF] = data;
}

word apu_read_wave(apu_t* __restrict pp, word addr)
{
    return pp->WVRAM[addr & 0xF];
}

#pragma endregion


static void apuch_update_length(struct apu_ch_t* __restrict ch)
{
    if(!ch->is_on)
        return;
    if(!(ch->NR_RAW[4] & 0x40))
        return;
    
    if(ch->length_ctr)
        --(ch->length_ctr);
    else
        ch->is_on = 0;
}

static void apuch_update_volenv(struct apu_ch_t* __restrict ch)
{
    if(!ch->is_on)
        return;
    if(!apuchi_get_volsweep_ctr(ch))
        return;
    
    if(ch->sweep_ctr)
    {
        if(--(ch->sweep_ctr));
            return;
    }
    
    ch->sweep_ctr = apuchi_get_volsweep_ctr(ch);
    
    if(apuchi_get_volsweep_is_increase(ch))
    {
        if(ch->vol < 0x10)
            ++(ch->vol);
    }
    else
    {
        if(ch->vol > 0)
            --(ch->vol);
    }
}

static sword apuch_tick_ch1(apu_t* __restrict pp, word _ch)
{
    struct apu_ch_t* __restrict ch = &pp->ch[_ch];
    
    if(ch->ctr)
        --(ch->ctr);
    else
    {
        ch->ctr = apuchi_get_reload(ch);
        
        ch->sample_no = (ch->sample_no + 1) & 7;
        
        ch->raw_out = (patterns[apuchi_get_ch1_sample_type(ch)] >> ch->sample_no) & 1;
        ch->dirty = 1;
    }
    
    return ch->raw_out * ch->vol;
}

static sword apuch_tick_ch3(apu_t* __restrict pp, word _ch)
{
    struct apu_ch_t* __restrict ch = &pp->ch[_ch];
    
    if(ch->ctr)
        --(ch->ctr);
    else
    {
        ch->ctr = apuchi_get_reload(ch) >> 1;
        
        ch->sample_no = (ch->sample_no + 1) & 31;
        
        s16 sample = pp->WVRAM[ch->sample_no >> 1];
        if(ch->sample_no & 1)
            sample &= 0x0F;
        else
            sample = (sample >> 4) & 0xF;
        
        switch((ch->NR_RAW[2] >> 5) & 3)
        {
            case 0: ch->raw_out = 0;             break;
            case 1: ch->raw_out = (sample);      break;
            case 2: ch->raw_out = (sample >> 1); break;
            case 3: ch->raw_out = (sample >> 2); break;
            
            default: __builtin_unreachable();
        }
        
        ch->dirty = 1;
    }
    
    return ch->raw_out;
}

static const r8 APUCH_CH4_MULDIV[8] PGB_DATA =
{
    2, 4, 8, 12, 16, 20, 24, 28
};

static sword apuch_tick_ch4(apu_t* __restrict pp, word _ch)
{
    struct apu_ch_t* __restrict ch = &pp->ch[_ch];
    
    if(!((ch->ctr)++ & ((1 << (ch->NR_RAW[3] >> 4)) - 1)))
    {
        if(--ch->sample_no)
            ;
        else
        {
            ch->ctr = 0;
            
            ch->sample_no = APUCH_CH4_MULDIV[(ch->NR_RAW[3] & 7)];
            
            r16 m = (1 << 14);
            if(ch->NR_RAW[3] & 8)
                m |= (1 << 6);
            
            r16 rs = ch->raw;
            r16 ns = rs >> 1;
            
            if((ns ^ rs) & 1)
                ns &= ~m;
            else
                ns |= m;
            ch->raw = ns;
            
            ch->raw_out = ch->raw & 1;
            ch->dirty = 1;
        }
    }
    
    if(ch->vol > 0x10)
    {
        //printf("ERROR: EARRAPE DETECTED AT %u\n", ch->vol);
        ch->vol = 1;
    }
    
    
    return ch->raw_out * ch->vol;
}

typedef sword(*pAPUChCb)(apu_t* __restrict pp, word _ch);

static void apu_render_add_ch(apu_t* __restrict pp, s16* outbuf, word ncounts, word ch, pAPUChCb chcb)
{
    if(!pp->ch[ch].is_on)
        return;
    
    word mcfg = (pp->MASTER_CFG >> 8) >> ch;
    
    if(!(mcfg & 5))
        return;
    
    s16 sample = chcb(pp, ch);
    r16 ctr = pp->ch[ch].ctr;
    
    if(!pp->ch[ch].is_on)
        return;
    
#if CONFIG_APU_MONO
    if(1)
    {
        s16 sample_l = (mcfg & 4) ? sample : 0;
        s16 sample_r = (mcfg & 1) ? sample : 0;
        
        *(outbuf++) += (sample_l + sample_r) >> 1;
    }
#else
    *(outbuf++) += (mcfg & 4) ? sample : 0;
    *(outbuf++) += (mcfg & 1) ? sample : 0;
#endif
    --ncounts;
    
    while(ncounts)
    {
        word dowork = ctr;
        if(ncounts < ctr)
            dowork = ncounts;
        
        ctr -= dowork;
        ncounts -= dowork;
        pp->ch[ch].ctr = ctr;
        
        s16 sample_l = (mcfg & 4) ? sample : 0;
        s16 sample_r = (mcfg & 1) ? sample : 0;
        
        while(dowork)
        {
        #if CONFIG_APU_MONO
            *(outbuf++) += (sample_l + sample_r) >> 1;
        #else
            *(outbuf++) += sample_l;
            *(outbuf++) += sample_r;
        #endif
            
            --dowork;
        }
        
        if(!ncounts)
            break;
        
        sample = chcb(pp, ch);
        ctr = pp->ch[ch].ctr;
        if(!pp->ch[ch].vol)
            break;
    }
}

void apu_render_faster(apu_t* __restrict pp, s16* outbuf, word ncounts)
{
    word realtotal = ncounts * APU_N_PER_TICK;
    word i, j;
    
    s16* buf = outbuf;
    
    if(!ncounts)
        return;
    
    for(i = 0; i != (realtotal * APU_N_CHANNELS); i++)
        *(buf++) = 0;
    
    word mcfg = pp->MASTER_CFG;
    
    if(!((mcfg >> 8) & 0xFF))
        return;
    
    apu_render_add_ch(pp, outbuf, realtotal, 0, apuch_tick_ch1);
    apu_render_add_ch(pp, outbuf, realtotal, 1, apuch_tick_ch1);
    apu_render_add_ch(pp, outbuf, realtotal, 2, apuch_tick_ch3);
    apu_render_add_ch(pp, outbuf, realtotal, 3, apuch_tick_ch4);
    
    buf = outbuf;
    
    s32 mul_r = ((mcfg >> 0) & 7) + 1;
    s32 mul_l = ((mcfg >> 4) & 7) + 1;
    
    for(i = 0; i != ncounts; i++)
    {
        s32 out_l = 0;
        s32 out_r = 0;
        
        for(j = 0; j != APU_N_PER_TICK; j++)
        {
#if CONFIG_APU_MONO
            s32 value = *(outbuf++);
            out_l += value;
            out_r += value;
#else
            out_l += *(outbuf++);
            out_r += *(outbuf++);
#endif
        }
        
        out_r *= mul_r;
        out_l *= mul_l;
        
#if CONFIG_APU_MONO
        *(buf++) = (out_l) + (out_r);
#else
        *(buf++) = out_l * 2;
        *(buf++) = out_r * 2;
#endif
    }
}

void apu_render(apu_t* __restrict pp, s16* outbuf, word ncounts)
{
    var i, j;
    
    word mcfg = pp->MASTER_CFG;
    
    for(j = 0; j != ncounts; j++)
    {
        s32 out_r = 0;
        s32 out_l = 0;
        
        for(i = 0; i != APU_N_PER_TICK; i++)
        {
            s16 s;
            
            if(pp->ch[0].is_on)
            {
                s = apuch_tick_ch1(pp, 0);
                if(mcfg & (1 << 8))
                    out_r += s;
                if(mcfg & (1 << 12))
                    out_l += s;
            }
            
            if(pp->ch[1].is_on)
            {
                s = apuch_tick_ch1(pp, 1);
                if(mcfg & (1 << 9))
                    out_r += s;
                if(mcfg & (1 << 13))
                    out_l += s;
            }
            
            if(pp->ch[2].is_on)
            {
                s = apuch_tick_ch3(pp, 2);
                if(mcfg & (1 << 10))
                    out_r += s;
                if(mcfg & (1 << 14))
                    out_l += s;
            }
            
            if(pp->ch[3].is_on)
            {
                s = apuch_tick_ch4(pp, 3);
                if(mcfg & (1 << 11))
                    out_r += s;
                if(mcfg & (1 << 15))
                    out_l += s;
            }
        }
        
        out_r *= ((mcfg >> 0) & 7) + 1;
        out_l *= ((mcfg >> 4) & 7) + 1;
        
#if CONFIG_APU_MONO
        *(outbuf++) = (out_l * 2) + (out_r * 2);
#else
        *(outbuf++) = out_l * 4;
        *(outbuf++) = out_r * 4;
#endif
    }
    
    if(!(pp->ch[0].is_on)) pp->MASTER_CFG &= 0xFFFEFFFF;
    if(!(pp->ch[1].is_on)) pp->MASTER_CFG &= 0xFFFDFFFF;
    if(!(pp->ch[2].is_on)) pp->MASTER_CFG &= 0xFFFBFFFF;
    if(!(pp->ch[3].is_on)) pp->MASTER_CFG &= 0xFFF7FFFF;
}

void apu_tick_internal_internals(apu_t* __restrict pp)
{
    if((pp->CTR_DIV & APU_DIV_256Hz))
        return;
    
    if(!(pp->CTR_DIV & APU_DIV_256Hz))
    {
        apuch_update_length(&pp->ch[0]);
        apuch_update_length(&pp->ch[1]);
        apuch_update_length(&pp->ch[2]);
        apuch_update_length(&pp->ch[3]);
    }
    
    if(!(pp->CTR_DIV & APU_DIV_64Hz))
    {
        apuch_update_volenv(&pp->ch[0]);
        apuch_update_volenv(&pp->ch[1]);
        apuch_update_volenv(&pp->ch[3]);
    }
    
    if(!(pp->CTR_DIV & APU_DIV_128Hz) && pp->ch[0].is_on && (pp->ch[0].NR_RAW[0] & 0x70))
    {
        if(pp->ch[0].raw)
            --(pp->ch[0].raw);
        else
        {
            struct apu_ch_t* ch0 = &pp->ch[0];
            
            //var reload = pp->ch[0].reload;
            var reload = ch0->NR_RAW[3] | ((ch0->NR_RAW[4] & 7) << 8);
            var newvar = (reload >> ((ch0->NR_RAW[0] & 7) + 0));
            
            ch0->raw = (ch0->NR_RAW[0] >> 4) & 7;
            
            //pp->ch[0].ctr = 0;
            
            if((ch0->NR_RAW[0] & 8))
            {
                if(newvar >= reload)
                    ch0->is_on = 0;
                else
                    reload -= newvar;
            }
            else
            {
                reload += newvar;
                if(0x7FF < reload)
                    reload = 0x7FF;
            }
            
            ch0->NR_RAW[3] = reload & 0xFF;
            ch0->NR_RAW[4] &= ~7;
            ch0->NR_RAW[4] |= (reload >> 8) & 7;
            
            apu_init_ch(pp, 0);
        }
    }
}

void apu_tick_internal(apu_t* __restrict pp)
{
    apu_tick_internal_internals(pp);
    
    if(pp->outbuf_pos >= pp->outbuf_size)
        return;
    
    apu_render(pp, &pp->outbuf[pp->outbuf_pos], 1);
    pp->outbuf_pos += APU_N_CHANNELS;
}

