#include "test.h"

static void
run_test(jit_state_t *j, uint8_t *arena_base, size_t arena_size)
{
#if __WORDSIZE > 32
  static uint32_t data[] = { 0xffffffff, 0x00000000, 0x42424242 };

  jit_begin(j, arena_base, arena_size);
  jit_load_args_1(j, jit_operand_gpr (JIT_OPERAND_ABI_WORD, JIT_R0));

  jit_ldxi_ui(j, JIT_R0, JIT_R0, (uintptr_t)data);
  jit_retr(j, JIT_R0);

  jit_uword_t (*f)(jit_uword_t) = jit_end(j, NULL);

  ASSERT(f(0) == data[0]);
  ASSERT(f(4) == data[1]);
  ASSERT(f(8) == data[2]);
#endif
}

int
main (int argc, char *argv[])
{
  return main_helper(argc, argv, run_test);
}
