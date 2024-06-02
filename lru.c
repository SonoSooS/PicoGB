#include "lru.h"


PGB_FUNC void lru_init(struct lru_state* lru)
{
    var i;
    
    lru->age_current = 1;
    for(i = 0; i != lru->slots_count; i++)
    {
        lru->slots[i].age = 0;
        lru->slots[i].token_address = ~0;
        lru->slots[i].token_bank = 0;
    }
}

PGB_FUNC struct lru_slot* lru_get_read(struct lru_state* lru, word address, word bank)
{
    var i;
    
    if(address < 0x4000)
        bank = 0;
    
    for(i = 0; i != lru->slots_count; i++)
    {
        struct lru_slot* slot = &lru->slots[i];
        
        if(slot->token_bank != bank)
            continue;
        if(slot->token_address != address)
            continue;
        
        return slot;
    }
    
    return NULL;
}

PGB_FUNC struct lru_slot* lru_get_write(struct lru_state* lru, word address, word bank, var* isnew)
{
    var i;
    
    struct lru_slot* slot;
    var current_age = lru->age_current;
    var oldest_i = 0;
    var oldest_age = 0;
    
    if(address < 0x4000)
        bank = 0;
    
    for(i = 0; i != lru->slots_count; i++)
    {
        slot = &lru->slots[i];
        
        if(slot->token_bank != bank || slot->token_address != address)
        {
            var age_offs = current_age - slot->age; // overflow behavior
            if(age_offs > oldest_age)
            {
                oldest_i = i;
                oldest_age = age_offs;
            }
            
            continue;
        }
        
        if(isnew)
            *isnew = 0;
        return slot;
    }
    
    slot = &lru->slots[oldest_i];
    //slot->token_address = address;
    //slot->token_bank = bank;
    slot->age = (lru->age_current)++;
    if(isnew)
        *isnew = 1;
    return slot;
}
