#include "kernel/types.h"

struct Stack {
  void* buffer;
};


void* ustack_malloc(uint len);

int ustack_free(void);