#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    exit(-1);

  //////swap in///////

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  // Handle page fault
  if((which_dev == 0) && (r_scause() == 13 || r_scause() == 15)){
    // Get the faulting address
    uint64 va = r_stval();

    // Find the page table entry (PTE) for the faulting virtual address
    pte_t *pte = walk(p->pagetable, va, 0);
    if (!pte || (*pte & PTE_V) != 0) {
      panic("usertrap: Invalid page table entry or not a page fault");
    }

    // Check if the page has been swapped out
    if (*pte & PTE_PG) {
      // Allocate a new physical page
      char *mem = kalloc(); // kalloc or ustack_malloc ???
      if (mem == 0) {
        panic("usertrap: Out of memory");
      }

    // Read the page content from the swap file
    uint64 swap_offset = *pte & PTE_PG;
    readFromSwapFile(p, mem, swap_offset, PGSIZE);


      // Update the page table entry to mark the page as present and set the physical address
      *pte = PTE_FLAGS(*pte) | PTE_V | PA2PTE(mem);

      // Free the swap file space occupied by the page
      removeSwapFile(p); // or removeSwapFile(p); ???
      // Update the FIFO queue variables
      p->paging_meta.queue_head = (p->paging_meta.queue_head + 1) % MAX_TOTAL_PAGES;
      p->paging_meta.queue_size--;
    } else { // TODO
      exit(-1);
      // It's a segmentation fault, handle accordingly
      // ...
    }

    // Retry the faulting instruction
    p->trapframe->epc = p->trapframe->epc + 4;
    return;
  }

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to userret in trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

// Function to select a page to swap out using FIFO algorithm
uint64 select_page_to_swap(struct proc *p) {
  // Get the virtual address of the oldest page in the FIFO queue
  uint64 va = p->paging_meta.swap_offset[p->paging_meta.queue_head];

  return va;
}



// Function to swap out a page from physical memory to the swap file
void swap_out_page(struct proc *p) {
  // Select a page to swap out (e.g., using LRU algorithm)
  uint64 va = select_page_to_swap(p); // TODO

  // Find the page table entry (PTE) for the virtual address
  pte_t *pte = walk(p->pagetable, va, 0);
  if (!pte || (*pte & PTE_V) == 0) {
    panic("swap_out_page: Invalid page table entry");
  }

  // Allocate a new physical page for swapping out
  char *mem = kalloc(); // kalloc or ustack_malloc ???
  if (mem == 0) {
    panic("swap_out_page: Out of memory");
  }

  // Copy the page content from physical memory to the allocated memory
  memmove(mem, (char*)PTE2PA(*pte), PGSIZE);

  // Write the page content to the swap file
  uint64 swap_offset = writeToSwapFile(p, mem, p->paging_meta.num_swapped_out_pages * PGSIZE, PGSIZE);

  
  // Update the page table entry to mark the page as swapped out
  *pte = (*pte & ~PTE_V) | PTE_PG | swap_offset;

  // Free the physical memory occupied by the swapped-out page
  kfree(mem); // kfree or ustack_free ???
  //ustack_free(mem);

  // Update the paging metadata
  p->paging_meta.swap_offset[p->paging_meta.num_swapped_out_pages++] = swap_offset;
  p->paging_meta. queue_head = (p->paging_meta.queue_head + 1) % MAX_TOTAL_PAGES;
  p->paging_meta.queue_size--;
}

