#pragma once

#include "types.h"
#include "mi.h"


#define MB_FMC_MODE_NONE 0
#define MB_FMC_MODE_ADD_r16 1
#define MB_FMC_MODE_SUB_r8 2
#define MB_FMC_MODE_ADD_r8 3
//#define MB_FMC_MODE_NONE_4 4
#define MB_FMC_MODE_ADD_r16_r8 5
#define MB_FMC_MODE_ADC_r8 6
#define MB_FMC_MODE_SBC_r8 7

#define MB_FMC_MODE_MAX 4

#define MICACHE_R_RESET 0xFFFF
#define MICACHE_R_BITS 12
#define MICACHE_R_SEL ((1 << 12) - 1)
#define MICACHE_R_VALUE(v) ((v) >> MICACHE_R_BITS)

struct mb_mi_cache
{
    const r8* __restrict mc_execute[MICACHE_R_VALUE(0x10000)];
    const r8* __restrict mc_read[MICACHE_R_VALUE(0x10000)];
    r8* __restrict mc_write[MICACHE_R_VALUE(0x10000)];
    void* _mc_dummy;
};

struct mb_state
{
    union
    {
        struct
        {
        r8 C, B, E, D, L, H, A, F; // A is this way because it's easier and faster (PUSH AF is rare)
        };
        struct
        {
        r16 BC, DE, HL, FA;
        };
        r8 raw8[8];
        r16 raw16[4];
        hilow16_t hilo16[4];
    } reg;
    hilow16_t IR;
    hilow16_t IMM;
    r16 PC;
    r16 SP;
    r8 IF;
    r8 IE;
    r8 IME;
    r8 IME_ASK;
    var DIV;
    var HALTING;
    r16 FR1;
    r16 FR2;
    var FMC_MODE;
    struct mi_dispatch* mi;
    struct mb_mi_cache micache; 
};
typedef struct mb_state mb_state;

word mb_exec(mb_state* __restrict);
void disasm(const mb_state* __restrict);


void micache_invalidate(struct mb_mi_cache* __restrict mic);
void micache_invalidate_range(struct mb_mi_cache* __restrict mic, word start, word end);

