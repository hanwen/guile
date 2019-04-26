#include "test.h"

static void
run_test(jit_state_t *j, uint8_t *arena_base, size_t arena_size)
{
#if __WORDSIZE > 32
  static uint32_t data[] = { 0xffffffff, 0x00000000, 0x42424242 };

  jit_begin(j, arena_base, arena_size);
  jit_load_args_1(j, jit_operand_gpr (JIT_OPERAND_ABI_POINTER, JIT_R1));

  jit_ldr_ui(j, JIT_R0, JIT_R1);
  jit_retr(j, JIT_R0);

  jit_uword_t (*f)(void*) = jit_end(j, NULL);

  ASSERT(f(&data[0]) == data[0]);
  ASSERT(f(&data[1]) == data[1]);
  ASSERT(f(&data[2]) == data[2]);
#endif
}

int
main (int argc, char *argv[])
{
  return main_helper(argc, argv, run_test);
}
