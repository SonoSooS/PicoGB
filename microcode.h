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


#define MB_FLAG_Z 0x80
#define MB_FLAG_N 0x40
#define MB_FLAG_H 0x20
#define MB_FLAG_C 0x10
#define MB_FLAG_BITS 0xF0

#define MB_CC_NZ 0
#define MB_CC_Z 1
#define MB_CC_NC 2
#define MB_CC_C 3

// Nothing cares about exec, works same as read
#define MB_TYPE_READ 0
#define MB_TYPE_WRITE 1
#define MB_TYPE_IS_READ(type) (!(type))
#define MB_TYPE_IS_WRITE(type) (!!(type))

#define MB_DATA_DONTCARE 0


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
void mb_disasm(const mb_state* __restrict);


void micache_invalidate(struct mb_mi_cache* __restrict mic);
void micache_invalidate_range(struct mb_mi_cache* __restrict mic, word start, word end);


PGB_FUNC static inline word mbh_irq_get_pending(const mb_state* __restrict mb)
{
    return mb->IE & mb->IF;
}
