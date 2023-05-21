#include "kernel/riscv.h"
#include "ustack.h"
#include "kernel/param.h"
#include "user.h"
#include <stdlib.h>

static struct Stack stack;

//
// Allocates a buffer of length len onto the stack and returns a pointer to
// the beginning of the buffer.If len is larger than the maximum allowed size of 
// an allocation operation, the function should return -1. 
// If a call to sbrk() fails, the function should return -1.
//

void* ustack_malloc(uint len) 
{
    if (len > MAXALLOCOP) 
    {
        return -1;
    }

    if (stack.buffer == NULL) 
    {
        stack.buffer = (void*)0;
    }

    uint npages = (len + PGSIZE - 1) / PGSIZE;
    uint alignedSize = npages * PGSIZE;

    // Allocating memory using sbrk
    void* buff = sbrk(alignedSize);
    if (buff == (void*)-1) 
    {
        return (void*)-1; 
    }
    stack.buffer = buff;
    return buff;
}

//
// Frees the last allocated buffer and pops it from the stack. After this call
// completes, any pointers to the popped buffer are invalid and must not
// be used.Should return -1 on error, and the length of the freed buffer on success.
// If the stack is empty, the function should return -1. When the last allocated 
// buffer is freed, all pages allocated by sbrk() should be released
// back to the kernel. A call to sbrk(-PGSIZE) will never fail and you may
// assume this in your code.
//

int ustack_free(void)
{
    if (stack.buffer == NULL) {
        return -1;  // Stack is empty
    }
    void* buffer = stack.buffer;

    // Calculate the required number of pages to free
    uint npages = (PGROUNDUP((uint)buffer) - PGROUNDDOWN((uint)buffer)) / PGSIZE;

    // Free the memory back to the kernel
    sbrk(-npages * PGSIZE);

    stack.buffer = NULL;  // Reset the buffer pointer

    return buffer != NULL ? (int)(PGSIZE * (((char*)buffer) - (char*)0)) : -1;
}