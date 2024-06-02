#include "mi.h"

PGB_FUNC void micache_invalidate(struct mb_mi_cache* __restrict mic)
{
    word counts = MICACHE_R_VALUE(0x10000);
    
    word i = 0;
    do
    {
        mic->mc_execute[i] = NULL;
        mic->mc_write[i] = NULL;
        mic->mc_read[i] = NULL;
    }
    while(++i < counts);
}

PGB_FUNC void micache_invalidate_range(struct mb_mi_cache* __restrict mic, word start, word end)
{
    word ends = MICACHE_R_VALUE(end - 1);
    
    word i = MICACHE_R_VALUE(start);
    do
    {
        mic->mc_execute[i] = NULL;
        mic->mc_write[i] = NULL;
        mic->mc_read[i] = NULL;
    }
    while(++i <= ends);
}
