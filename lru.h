#pragma once

#include "types.h"



struct lru_slot
{
    r8* data;
    var age;
    r16 token_address;
    r16 token_bank;
#if GBA
    var _padding[3];
#else
    word _padding[1];
#endif
};

struct lru_state
{
    struct lru_slot* slots;
    var slots_count;
    var age_current;
};

void lru_init(struct lru_state* lru);
struct lru_slot* lru_get_read(struct lru_state* lru, word address, word bank);
struct lru_slot* lru_get_write(struct lru_state* lru, word address, word bank, var* isnew);

static inline void lru_update_slot(struct lru_slot* slot, word address, word bank)
{
    if(address < 0x4000)
        bank = 0;
    
    slot->token_address = address;
    slot->token_bank = bank;
}
