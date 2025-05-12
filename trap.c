#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

#define MAP_PROT_READ  0x00000001
#define MAP_PROT_WRITE 0x00000002

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}


//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
//  case T_IRQ0 + IRQ_IDE2:
//	ide2intr();
//	lpaiceoi();
//	break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;
  case T_PGFLT: 
  {
    // If page fault happens, this code will run

    uint va = rcr2(); // Address which caused the page fault is stored in cr2. User rcr2() to get it.
    va = PGROUNDDOWN(va); // PTEs only store multiples of pagesizes. Use PGROUNDDOWN to round down.
    struct proc *p = myproc(); 
    pte_t *pte = walkpgdir2(p->pgdir, (void*)va, 0); // get pte from va

    // Case 1: CoW
    if(pte && (*pte & PTE_P) && !(*pte & PTE_W) && (*pte & PTE_COW)){
      // check if present, currently non-writable, and is marked CoW
      uint pa = PTE_ADDR(*pte); // get physical address from pte
      int i = pa/PGSIZE; // get index of pa in pageframe_counters array
      
      // check if counter more than 1. Else if 1, it means that no other process references this pf. No need to copy.
      if(pageframe_counters[i] > 1){ 
        char *newpa = kalloc(); // get a free page frame
        if(newpa == 0)
          panic("CoW: kalloc failed");
        memmove(newpa, (char*)P2V(pa), PGSIZE); // copy contents from parent pageframe to the free pageframe
        pageframe_counters[i]--;
        *pte = (V2P(newpa) | PTE_P | PTE_W | PTE_U) & ~PTE_COW; // this is code from the original copyuvm() function
                                                                // set to present, writable, and user, then remove cow
        lcr3(V2P(p->pgdir)); // flush TLB (reset/update TLB)
        return;
      }
      else // Counter is 1. Process is the sole owner of the page (eg. child is dead, so parent is the only one
           // referencing this page frame.). We can safely set it to writable without worrying about other processes.  
      {
        *pte |= PTE_W;       // Enable write
        *pte &= ~PTE_COW;    // Remove CoW bit
        lcr3(V2P(p->pgdir)); // flush TLB (reset/update TLB)
        return;
      }
    }


    // Case 2: mmap area
    // Find mmap area
    struct mmap_area *ma;
    int i = 0;
    while (i < MAX_MMAPS_PROC)
    {
      if (va == p->mmaps[i].addr){
        ma = &p->mmaps[i];
        break;
      }

      i++;
    }

    if (i == MAX_MMAPS_PROC) // Not found mmap area, invalid access
      goto bad;

    // If writable
    if (ma->flags & MAP_PROT_WRITE)
    {
      pte = walkpgdir2(p->pgdir, (char*)va, 0);
      *pte |= PTE_W;
      lcr3(V2P(p->pgdir));     
      ma->dirty = 1;
      return;
    }
    
    // If not CoW, and not mmap writable, invalid access.
    // Process might have tried to read or write to protected addresses, such as kernel addresses,
    // or tried to write to read-only mmap areas.
    bad:
      cprintf("bad page fault pid=%d name=%s va=0x%x eip=0x%x cs=0x%x err=0x%x ",
        p->pid, p->name, va, tf->eip, tf->cs, tf->err);
      if (pte)
        cprintf("PTE=*0x%x\n", *pte);
      else
        cprintf("no PTE!\n");
      p->killed = 1;
      break;
  }
    

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
