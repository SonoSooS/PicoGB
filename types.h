#pragma once

#include "config.h"


#define ATTR_HOT __attribute__((hot))

//#define ATTR_FORCE_INLINE __attribute__((always_inline))
#define ATTR_FORCE_INLINE
//#define ATTR_FORCE_NOINLINE __attribute__((noinline))
#define ATTR_FORCE_NOINLINE 
//#define COMPILER_VARIABLE_BARRIER(var) (void)(var)
#define COMPILER_VARIABLE_BARRIER(var) __asm volatile(""::"r"(var))
//#define COMPILER_MEMORY_BARRIER() 
#define COMPILER_MEMORY_BARRIER() __asm volatile("":::"memory")
//#define COMPILER_UNLIKELY(cond) cond
#define COMPILER_UNLIKELY(cond) __builtin_expect(cond, 0)
//#define COMPILER_LIKELY(cond) cond
#define COMPILER_LIKELY(cond) __builtin_expect(cond, 1)


#ifndef NULL
#define NULL ((void*)0)
#endif

#ifndef count_of
#define count_of(h) (sizeof(h)/sizeof((h)[0]))
#endif


#if PPU_IS_MONOCHROME
typedef unsigned char pixel_t;
#elif PPU_MODE == 4
typedef unsigned char pixel_t;
#elif PPU_MODE == 6
typedef unsigned short pixel_t;
#else
typedef unsigned int pixel_t;
#endif

typedef unsigned int word;
typedef signed int sword;
#if !GBA
typedef unsigned int var;
typedef signed int svar;
#else
//typedef unsigned short var;
//typedef signed short svar;
typedef unsigned int var;
typedef signed int svar;
#endif

typedef word wbool;
typedef var vbool;

typedef unsigned char r8;
typedef unsigned short r16;
typedef unsigned long int r32;

typedef signed short s16;
typedef signed long int s32;

struct hilow16_t
{
    union
    {
        struct
        {
        r8 low, high;
        };
        r16 raw;
    };
};
typedef struct hilow16_t hilow16_t;
