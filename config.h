#pragma once

//#define PICOGB_RP2 1

#if defined(PICOGB_PD) || defined(TARGET_SIMULATOR)
#define NO_ASSERT
#include "config_pd.h"
#elif defined(PICO_NO_FPGA_CHECK) || defined(PICOGB_RP2)
#include "config_rp2.h"
#else
//#define CONFIG_DBG
//#define CONFIG_DBG_IO
//#define GBA
//#define PPU_SCANLINE_UPDATES
//#define CONFIG_MIC_CACHE_BYPASS
//#define CONFIG_DOCTOR
//#define CONFIG_LYC_90
//#define CONFIG_BOOTMEME
#define CONFIG_NOBOOTMEME
//#define CONFIG_IS_CGB
//#define CONFIG_FORCE_ENABLE_CGB
#define CONFIG_PPU_CGB_MONO
#define CONFIG_VSYNC 2
#define CONFIG_PPU_INVERT
#define CONFIG_APU_ENABLE
#define CONFIG_APU_RICH
#define CONFIG_APU_MONO
//#define CONFIG_ENABLE_LRU

// 0 - none
// 1 - record
// 2 - playback
#define CONFIG_TAS 0

// 0 - default 32bit full-color
// 1 - 1bpp jankdither globally fixed
// 2 - 1bpp nodither
// 3 - 1bpp jankdither locally fixed
// 4 - 2x4bpp ANSI 16-color packed, doubled
// 5 - 8bpp ANSI 16-color (low nybble), single
// 6 - RGB565
#define PPU_MODE 0

// 0 - no interlacing
// 1 - full frame interlacing (half FPS)
// 2 - actual interlacing
#define PPU_INTERLACE 0

#endif

#ifdef NO_ASSERT
#undef assert
#define assert(h) ;
#endif

#ifndef PPU_MODE
#define PPU_MODE 0
#endif

#if PPU_MODE == 1 || PPU_MODE == 2 || PPU_MODE == 3
#define PPU_IS_MONOCHROME 1
#define PPU_IS_BPP 1
#define PPU_IS_IPP 8
#define PPU_IS_SPP 1

#if PPU_MODE == 1 || PPU_MODE == 3
#define PPU_IS_DITHER 1
#else
#define PPU_IS_DITHER 0
#endif

#else
#define PPU_IS_MONOCHROME 0
#define PPU_IS_BPP (sizeof(pixel_t)*8)
#define PPU_IS_IPP 1
#define PPU_IS_SPP 8
#define PPU_IS_DITHER 0
#endif

#ifndef GBA
#define GBA 0
#else
#undef GBA
#define GBA 1
#endif

#ifndef CONFIG_DBG
#define CONFIG_DBG 0
#else
#undef CONFIG_DBG
#define CONFIG_DBG 1
#endif

#ifndef CONFIG_DBG_IO
#define CONFIG_DBG_IO 0
#else
#undef CONFIG_DBG_IO
#define CONFIG_DBG_IO 1
#endif

#ifndef PPU_SCANLINE_UPDATES
#define PPU_SCANLINE_UPDATES 0
#else
#undef PPU_SCANLINE_UPDATES
#define PPU_SCANLINE_UPDATES 1
#endif

#ifndef CONFIG_MIC_CACHE_BYPASS
#define CONFIG_MIC_CACHE_BYPASS 0
#else
#undef CONFIG_MIC_CACHE_BYPASS
#define CONFIG_MIC_CACHE_BYPASS 1
#endif

#ifndef CONFIG_DOCTOR
#define CONFIG_DOCTOR 0
#else
#undef CONFIG_DOCTOR
#define CONFIG_DOCTOR 1
#endif

#ifndef CONFIG_LYC_90
#define CONFIG_LYC_90 0
#else
#undef CONFIG_LYC_90
#define CONFIG_LYC_90 1
#endif

#ifndef CONFIG_BOOTMEME
#define CONFIG_BOOTMEME 0
#else
#undef CONFIG_BOOTMEME
#define CONFIG_BOOTMEME 1
#endif

#ifndef CONFIG_NOBOOTMEME
#define CONFIG_NOBOOTMEME 0
#else
#undef CONFIG_NOBOOTMEME
#define CONFIG_NOBOOTMEME 1
#endif

#ifndef CONFIG_IS_CGB
#define CONFIG_IS_CGB 0
#else
#undef CONFIG_IS_CGB
#define CONFIG_IS_CGB 1
#endif

#ifndef CONFIG_PPU_CGB_MONO
#define CONFIG_PPU_CGB_MONO 0
#else
#undef CONFIG_PPU_CGB_MONO
#define CONFIG_PPU_CGB_MONO 1
#endif

#ifndef CONFIG_VSYNC
#define CONFIG_VSYNC 0
#endif

#ifndef CONFIG_PPU_INVERT
#define CONFIG_PPU_INVERT 0
#else
#undef CONFIG_PPU_INVERT
#define CONFIG_PPU_INVERT 1
#endif

#ifndef CONFIG_APU_ENABLE
#define CONFIG_APU_ENABLE 0
#else
#undef CONFIG_APU_ENABLE
#define CONFIG_APU_ENABLE 1
#endif

#ifndef CONFIG_APU_RICH
#define CONFIG_APU_RICH 0
#else
#undef CONFIG_APU_RICH
#define CONFIG_APU_RICH 1
#endif

#ifndef CONFIG_APU_MONO
#define CONFIG_APU_MONO 0
#else
#undef CONFIG_APU_MONO
#define CONFIG_APU_MONO 1
#endif

#ifndef PPU_INTERLACE
#define PPU_INTERLACE 0
#endif

#ifndef CONFIG_ENABLE_LRU
#define CONFIG_ENABLE_LRU 0
#else
#undef CONFIG_ENABLE_LRU
#define CONFIG_ENABLE_LRU 1
#endif

#ifndef PGB_FUNC
#define PGB_FUNC
#endif

#ifndef PGB_DATA
#define PGB_DATA
#endif

#ifndef CONFIG_FORCE_ENABLE_CGB
#define CONFIG_FORCE_ENABLE_CGB CONFIG_IS_CGB
#else
#undef CONFIG_FORCE_ENABLE_CGB
#define CONFIG_FORCE_ENABLE_CGB 1
#endif
