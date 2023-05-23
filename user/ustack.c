// #include "kernel/riscv.h"
// #include "ustack.h"
// #include "kernel/param.h"
// #include "user.h"
// #include <stdlib.h>

// static struct Stack stack;
// typedef struct {
//     int* buffer_array;
//     int top;
//     int capacity;//?
// } Stack;

// //
// // Allocates a buffer of length len onto the stack and returns a pointer to
// // the beginning of the buffer.If len is larger than the maximum allowed size of 
// // an allocation operation, the function should return -1. 
// // If a call to sbrk() fails, the function should return -1.
// //

// void* ustack_malloc(uint len) 
// {
//     if (len > MAXALLOCOP) 
//     {
//         return -1;
//     }

//     if (stack.buffer == NULL) 
//     {
//         stack.buffer = (void*)0;
//     }

//     uint npages = (len + PGSIZE - 1) / PGSIZE;
//     uint alignedSize = npages * PGSIZE;

//     // Allocating memory using sbrk
//     void* buff = sbrk(alignedSize);
//     if (buff == (void*)-1) 
//     {
//         return (void*)-1; 
//     }
//     stack.buffer = buff;
//     return buff;
// }

// //
// // Frees the last allocated buffer and pops it from the stack. After this call
// // completes, any pointers to the popped buffer are invalid and must not
// // be used.Should return -1 on error, and the length of the freed buffer on success.
// // If the stack is empty, the function should return -1. When the last allocated 
// // buffer is freed, all pages allocated by sbrk() should be released
// // back to the kernel. A call to sbrk(-PGSIZE) will never fail and you may
// // assume this in your code.
// //

// int ustack_free(void)
// {
//     if (stack.buffer == NULL) {
//         return -1;  // Stack is empty
//     }
//     void* buffer = stack.buffer;

//     // Calculate the required number of pages to free
//     uint npages = (PGROUNDUP((uint)buffer) - PGROUNDDOWN((uint)buffer)) / PGSIZE;

//     // Free the memory back to the kernel
//     sbrk(-npages * PGSIZE);

//     stack.buffer = NULL;  // Reset the buffer pointer

//     return buffer != NULL ? (int)(PGSIZE * (((char*)buffer) - (char*)0)) : -1;
// }

#include "kernel/types.h"
#include "kernel/riscv.h"

#define MAX_BUFFERS 8
//#define PGSIZE 4096

extern char end[]; // Defined by the kernel linker script
static char* stack_top = 0; // Global variable to track the top of the stack
static void* buffers[MAX_BUFFERS]; // Array to store pointers to allocated buffers
static uint buffer_lengths[MAX_BUFFERS]; // Array to store lengths of allocated buffers
static int buffer_count = 0; // Counter to keep track of the number of allocated buffers

int ustack_free(void) {
  // Check if the stack is empty
  if (buffer_count == 0)
    return -1;

  // Get the index of the last allocated buffer
  int index = buffer_count - 1;

  // Free the last allocated buffer and update the stack top
  void* buffer = buffers[index];
  stack_top = (char*)buffer;
  buffer_count--;

  // Calculate the length of the freed buffer
  int len = buffer_lengths[index];

  // Check if the last allocated buffer is freed
  if (stack_top == end) {
    // Release all pages allocated by sbrk() back to the kernel
    while (buffer_count > 0 && stack_top == end) {
      stack_top -= PGSIZE;
      buffer_count--;
    }
    sbrk(-(PGSIZE * buffer_count));
  }

  return len;
}

void* ustack_malloc(uint len) {
  // Check if the requested length exceeds the maximum allowed size
  if (len > 512)
    return (void*)-1;

  // Check if the maximum number of buffers has been reached
  if (buffer_count == MAX_BUFFERS)
    return (void*)-1;

  // Check if the heap area exists
  if (stack_top == 0 || stack_top <= end) {
    // Allocate a new page from the kernel
    if (sbrk(PGSIZE) == (void*)-1)
      return (void*)-1;

    stack_top = (char*)(end + PGSIZE);
  }

  // Calculate the start address of the buffer
  char* buffer_start = stack_top - len;

  // Check if the requested length exceeds the remaining stack space
  if (buffer_start < end)
    return (void*)-1;

  // Allocate the buffer from the stack
  void* buffer = (void*)buffer_start;
  stack_top = buffer_start;

  // Store the buffer and its length in the arrays
  buffers[buffer_count] = buffer;
  buffer_lengths[buffer_count] = len;
  buffer_count++;

  return buffer;
}
