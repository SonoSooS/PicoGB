#pragma once

//#define CONFIG_DBG
//#define CONFIG_DBG_IO
//#define PPU_SCANLINE_UPDATES
//#define CONFIG_MIC_CACHE_BYPASS
//#define CONFIG_DOCTOR
//#define CONFIG_LYC_90
//#define CONFIG_BOOTMEME
#define CONFIG_NOBOOTMEME
//#define CONFIG_IS_CGB
//#define CONFIG_FORCE_ENABLE_CGB
//#define CONFIG_PPU_CGB_MONO
#define CONFIG_VSYNC 2
#define CONFIG_PPU_INVERT
#define CONFIG_APU_ENABLE_PARTIAL
#define CONFIG_APU_ENABLE
#define CONFIG_APU_RICH
//#define CONFIG_APU_MONO
#define CONFIG_APU_N_PER_TICK 8
#define CONFIG_APU_N_BUFSIZE 32768
//#define CONFIG_ENABLE_LRU
//#define CONFIG_PPU_ACTION_ON_START

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