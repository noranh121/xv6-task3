#include "kernel/types.h"
#include "kernel/param.h"
#include "user.h"
#include "kernel/riscv.h"

struct Stack {
  void* buffer;
};


void* ustack_malloc(uint len);

int ustack_free(void);