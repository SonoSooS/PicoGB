#pragma once

#define PICOGB_RP2 1

//#define CONFIG_DBG
//#define CONFIG_DBG_IO
//#define GBA
//#define PPU_SCANLINE_UPDATES
//#define CONFIG_DOCTOR
//#define CONFIG_LYC_90
//#define CONFIG_BOOTMEME
#define CONFIG_NOBOOTMEME
//#define CONFIG_IS_CGB
#define CONFIG_VSYNC
#define CONFIG_PPU_INVERT
#define CONFIG_ENABLE_LRU

// 0 - default 32bit full-color
// 1 - 1bpp jankdither globally fixed
// 2 - 1bpp nodither
// 3 - 1bpp jankdither locally fixed
// 4 - 2x4bpp ANSI 16-color packed, doubled
// 5 - 8bpp ANSI 16-color (low nybble), single
// 6 - RGB565
#define PPU_MODE 6
