#include "apu.h"



void apu_reset(apu_t* __restrict pp);
void apu_initialize(apu_t* __restrict pp);


/*
#define APU_DIV_512Hz 0x7F8
#define APU_DIV_256Hz 0xFF8
#define APU_DIV_128Hz 0x1FF8
#define APU_DIV_64Hz 0x3FF8
*/

#define APU_DIV_512Hz (0x3FF & ~(APU_N_PER_TICK - 1))
#define APU_DIV_256Hz (0x7FF & ~(APU_N_PER_TICK - 1))
#define APU_DIV_128Hz (0xFFF & ~(APU_N_PER_TICK - 1))
#define APU_DIV_64Hz  (0x1FFF & ~(APU_N_PER_TICK - 1))

#define APU_BIAS 8


static const var patterns[4] =
{
    0b11111110,
    0b01111110,
    0b01111000,
    0b10000001
};

#pragma region Collapsible shit

void apu_reset(apu_t* __restrict pp)
{
    pp->ch[0].sample_no = 0;
    pp->ch[1].sample_no = 0;
    pp->ch[2].sample_no = 0;
    pp->ch[3].sample_no = 0;
    
    pp->ch[0].vol = 0;
    pp->ch[1].vol = 0;
    pp->ch[2].vol = 0;
    pp->ch[3].vol = 0;
    
    pp->MASTER_CFG &= 0xFF70FFFF;
    
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
    
    if(!(ch->NR_RAW[4] & 0x80))
        return;
    
    ch->vol = 1;
    
    pp->MASTER_CFG |= 1 << (_ch + 24);
    
    ch->length_ctr = (~ch->NR_RAW[1]) & 0xFF;
    ch->sweep_ctr = 0;
    
    ch->reload = (~(ch->NR_RAW[3] | (ch->NR_RAW[4] << 8)) & 0x7FF) >> 1; //HACK: 2MHz DAC bleh
    ch->ctr = ch->reload;
    ch->sample_no = 1;
    
    ch->raw = (ch->NR_RAW[2] >> 5) & 3;
}

#include <stdio.h>

static void apu_init_ch(apu_t* __restrict pp, word _ch)
{
    struct apu_ch_t* __restrict ch = &pp->ch[_ch];
    
    /*
    if(_ch == 0)
    printf("trigger happy %02X %02X %02X %02X %02X\n",
            ch->NR_RAW[0],
            ch->NR_RAW[1],
            ch->NR_RAW[2],
            ch->NR_RAW[3],
            ch->NR_RAW[4]
            );
    */
    
    if(!(ch->NR_RAW[4] & 0x80))
        return;
    
    ch->vol = (ch->NR_RAW[2] >> 4) + ((ch->NR_RAW[2] >> 3) & 1);
    if(!ch->vol)
        return;
    
    pp->MASTER_CFG |= 1 << (_ch + 24);
    
    ch->sample_type = ch->NR_RAW[1] >> 6;
    ch->length_ctr = (ch->NR_RAW[1]) & 0x3F;
    ch->sweep_ctr = ch->NR_RAW[2] & 7;
    
    if(_ch != 3)
    {
        ch->reload = ~(ch->NR_RAW[3] | (ch->NR_RAW[4] << 8)) & 0x7FF;
        ch->ctr = ch->reload;
        ch->sample_no = 0;
    }
    else
    {
        ch->reload = 0;
        ch->ctr = 0;
        ch->sample_no = 0;
    }
    
    if(_ch == 0)
        ch->raw = ch->NR_RAW[0];
    else if(_ch == 3)
        ch->raw = 0;
}

void apu_write(apu_t* __restrict pp, word addr, word data)
{
    addr = (addr - 0x10) & 0x1F;
    
    switch(addr)
    {
        case 0: pp->ch[0].NR_RAW[0] = data; break;
        case 1: pp->ch[0].NR_RAW[1] = data; break;
        case 2: pp->ch[0].NR_RAW[2] = data; if(!(data & 0xF8)) pp->ch[0].vol = 0; break;
        case 3: pp->ch[0].NR_RAW[3] = data; break;
        case 4: pp->ch[0].NR_RAW[4] = data;
        {
            apu_init_ch(pp, 0);
            break;
        }
        
        case 5: break;
        case 6: pp->ch[1].NR_RAW[1] = data; break;
        case 7: pp->ch[1].NR_RAW[2] = data; if(!(data & 0xF8)) pp->ch[1].vol = 0; break;
        case 8: pp->ch[1].NR_RAW[3] = data; break;
        case 9: pp->ch[1].NR_RAW[4] = data;
        {
            apu_init_ch(pp, 1);
            break;
        }
        
        case 10: pp->ch[2].NR_RAW[0] = data; if(!(data & 0x80)) pp->ch[2].vol = 0; break;
        case 11: pp->ch[2].NR_RAW[1] = data; break;
        case 12: pp->ch[2].NR_RAW[2] = data; break;
        case 13: pp->ch[2].NR_RAW[3] = data; break;
        case 14: pp->ch[2].NR_RAW[4] = data;
        {
            apu_init_ch3(pp, 2);
            break;
        }
        
        case 15: break;
        case 16: pp->ch[3].NR_RAW[1] = data; break;
        case 17: pp->ch[3].NR_RAW[2] = data; if(!(data & 0xF8)) pp->ch[3].vol = 0; break;
        case 18: pp->ch[3].NR_RAW[3] = data; break;
        case 19: pp->ch[3].NR_RAW[4] = data;
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

word apu_read(apu_t* __restrict pp, word addr)
{
    addr = (addr - 0x10) & 0x1F;
    
    switch(addr)
    {
        case 0: return pp->ch[0].NR_RAW[0] | 0x80;
        case 1: return pp->ch[0].NR_RAW[1] | 0xBF;
        case 2: return pp->ch[0].NR_RAW[2];
        case 3: return pp->ch[0].NR_RAW[3] | 0xFF;
        case 4: return pp->ch[0].NR_RAW[4] | 0xBF;
        
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
            return ((pp->MASTER_CFG >> 16) & 0x8F) | 0x70;
        
        default:
            __builtin_unreachable();
            return 0xFF;
    }
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
    if(!ch->vol)
        return;
    if(!(ch->NR_RAW[4] & 0x40))
        return;
    
    if(ch->length_ctr)
        --(ch->length_ctr);
    else
        ch->vol = 0;
}

static void apuch_update_volenv(struct apu_ch_t* __restrict ch)
{
    if(!ch->vol)
        return;
    if(!(ch->NR_RAW[2] & 7))
        return;
    
    if(ch->sweep_ctr)
    {
        --(ch->sweep_ctr);
        return;
    }
    
    ch->sweep_ctr = (ch->NR_RAW[2] & 7) + 1;
    
    if(ch->NR_RAW[2] & 0x8)
    {
        if(ch->vol < 0x10)
            ++(ch->vol);
    }
    else
    {
        //if(ch->vol > 1)
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
        ch->ctr = ch->reload;
        
        ch->sample_no = (ch->sample_no + 1) & 7;
    }
    
    return (((patterns[ch->sample_type] >> ch->sample_no) & 1) ? -1 : 0) * ch->vol;
}

static sword apuch_tick_ch3(apu_t* __restrict pp, word _ch)
{
    struct apu_ch_t* __restrict ch = &pp->ch[_ch];
    
    if(ch->ctr)
        --(ch->ctr);
    else
    {
        ch->ctr = ch->reload;
        
        ch->sample_no = (ch->sample_no + 1) & 31;
    }
    
    s16 sample = pp->WVRAM[ch->sample_no >> 1];
    if(ch->sample_no & 1)
        sample &= 0x0F;
    else
        sample = (sample >> 4) & 0xF;
    
    switch(ch->raw & 3)
    {
        case 0: return 0;
        case 1: return sample - 8;
        case 2: return (sample >> 1) - 4;
        case 3: return (sample >> 2) - 2;
        
        default: __builtin_unreachable();
    }
}

static sword apuch_tick_ch4(apu_t* __restrict pp, word _ch)
{
    struct apu_ch_t* __restrict ch = &pp->ch[_ch];
    
    if(!((ch->ctr)++ & ((1 << ((ch->NR_RAW[3] >> 4) + 0)) - 1)))
    {
        if(--ch->sample_no)
            ;
        else
        {
            ch->sample_no = (ch->NR_RAW[3] & 7);
            
            r16 rs = ch->raw;
            r16 ns = (((rs >> 1) ^ ~rs) & 1);
            
            if(ch->NR_RAW[3] & 8)
            {
                ns = (ns << 7) | (ns << 15);
                ch->raw = (ch->raw & 0x3FBF) | ns;
            }
            else
            {
                ch->raw = (((rs >> 1) ^ ~rs) << 14) | ((rs >> 1) & 0x3FFF);
            }
        }
    }
    
    if(ch->vol > 0x10)
    {
        //printf("ERROR: EARRAPE DETECTED AT %u\n", ch->vol);
        ch->vol = 1;
    }
    
    
    return ((ch->raw & 1) ? 1 : 0) * ch->vol;
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
            
            if(pp->ch[0].vol)
            {
                s = apuch_tick_ch1(pp, 0);
                if(mcfg & (1 << 8))
                    out_r += s;
                if(mcfg & (1 << 12))
                    out_l += s;
            }
            
            if(pp->ch[1].vol)
            {
                s = apuch_tick_ch1(pp, 1);
                if(mcfg & (1 << 9))
                    out_r += s;
                if(mcfg & (1 << 13))
                    out_l += s;
            }
            
            if(pp->ch[2].vol)
            {
                s = apuch_tick_ch3(pp, 2);
                if(mcfg & (1 << 10))
                    out_r += s;
                if(mcfg & (1 << 14))
                    out_l += s;
            }
            
            if(pp->ch[3].vol)
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
        
        *(outbuf++) = out_l * 1;
        *(outbuf++) = out_r * 1;
    }
}

void apu_tick_internal_internals(apu_t* __restrict pp)
{
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
    
    if(!(pp->CTR_DIV & APU_DIV_128Hz) && pp->ch[0].vol && (pp->ch[0].raw & 0x70))
    {
        /*printf("sweep sweep %02X %02X %02X %02X %02X\n",
            pp->ch[0].NR_RAW[0],
            pp->ch[0].NR_RAW[1],
            pp->ch[0].NR_RAW[2],
            pp->ch[0].NR_RAW[3],
            pp->ch[0].NR_RAW[4]
            );
        */
        
        if(pp->ch[0].sweep_ctr)
            --(pp->ch[0].sweep_ctr);
        else
        {
            var reload = pp->ch[0].reload;
            var newvar = (reload >> ((pp->ch[0].raw & 7) + 0));
            
            pp->ch[0].sweep_ctr = (pp->ch[0].raw >> 4) & 7;
            
            //pp->ch[0].ctr = 0;
            
            if(!(pp->ch[0].raw & 8))
            {
                if(newvar >= reload)
                    pp->ch[0].vol = 0;
                else
                    pp->ch[0].reload = reload - newvar;
            }
            else
            {
                reload += newvar;
                if(0x3FF < reload)
                //    pp->ch[0].vol = 0;
                    reload = 0x3FF;
                //else
                    pp->ch[0].reload = reload;
            }
        }
    }
    
    if(!(pp->ch[0].vol)) pp->MASTER_CFG &= 0xFFFEFFFF;
    if(!(pp->ch[1].vol)) pp->MASTER_CFG &= 0xFFFDFFFF;
    if(!(pp->ch[2].vol)) pp->MASTER_CFG &= 0xFFFBFFFF;
    if(!(pp->ch[3].vol)) pp->MASTER_CFG &= 0xFFF7FFFF;
}

void apu_tick_internal(apu_t* __restrict pp)
{
    apu_tick_internal_internals(pp);
    
    if(pp->outbuf_pos >= (sizeof(pp->outbuf)/sizeof(pp->outbuf[0])))
        return;
    
    apu_render(pp, &pp->outbuf[pp->outbuf_pos], 1);
    pp->outbuf_pos += 2;
}

