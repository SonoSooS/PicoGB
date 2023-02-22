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
//#define CONFIG_PPU_INVERT

// 0 - default 32bit full-color
// 1 - 1bpp jankdither globally fixed
// 2 - 1bpp nodither
// 3 - 1bpp jankdither locally fixed
#define PPU_MODE 4
