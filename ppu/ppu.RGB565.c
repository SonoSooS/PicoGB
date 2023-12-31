
static const pixel_t palette[] PGB_DATA =
//{0x1084, 0x21AA, 0x3EB3, 0x5FB0};
#define RGB565(a1, a2, a3) (((a1 >> 3) << (5 + 6)) | ((a2 >> 2) << 5) | ((a3 >> 3) << 0))
{
    __builtin_bswap16(RGB565(0x20, 0x20, 0x20)),
    __builtin_bswap16(RGB565(0x47, 0x69, 0x51)),
    __builtin_bswap16(RGB565(0x79, 0xAF, 0x98)),
    __builtin_bswap16(RGB565(0xBD, 0xEF, 0x86))
};
