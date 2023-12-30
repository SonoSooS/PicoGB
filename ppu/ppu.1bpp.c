
#if PPU_IS_DITHER
#if CONFIG_PPU_INVERT
static const pixel_t ditherbuf[4][4] PGB_DATA =
{
    {0, 0, 0, 0},
    {1, 0, 0, 0},
    {1, 0, 0, 1},
    {1, 1, 1, 1},
};
#else
static const pixel_t ditherbuf[4][4] PGB_DATA =
{
    {1, 1, 1, 1},
    {1, 0, 0, 1},
    {1, 0, 0, 0},
    {0, 0, 0, 0},
};
#endif
#endif
