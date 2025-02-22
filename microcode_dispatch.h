

word mch_memory_dispatch_read_Haddr(const mb_state* __restrict mb, word haddr);
void mch_memory_dispatch_write_Haddr(mb_state* __restrict mb, word haddr, word data);
word mch_memory_fetch_PC_op_2(mb_state* __restrict mb);
word mch_memory_fetch_PC_op_1(mb_state* __restrict mb);
word mch_memory_fetch_PC(mb_state* __restrict mb);
void mch_memory_dispatch_write(mb_state* __restrict mb, word addr, word data);
word mch_memory_dispatch_read(mb_state* __restrict mb, word addr);
