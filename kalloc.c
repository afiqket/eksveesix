// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

static int frees = 0;

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

#define NPAGEFRAMES (PHYSTOP / PGSIZE)
int pageframe_counters[NPAGEFRAMES];

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.

void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;

  // Initiate pageframe_counters
  for (int i = 0; i < NPAGEFRAMES; i++)
    pageframe_counters[i] = 0;

  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // Get counter index
  int i = ((uint)v - KERNBASE) / PGSIZE;

  // Decrease counter when kfree is called
  // If counter = 0, don't decrease it. 
  if(pageframe_counters[i] != 0)
  {
    pageframe_counters[i]--;
  }
  
  // If decreased, and the counter is now 0, this means that no processes reference this frame anymore.
  // Now we should actually free it now.
  // If decreased, and the counter is still 1 or more, that means 1 or more processes still reference this frame.
  // We should not free it.
  if(pageframe_counters[i] == 0)
  {
    // Fill with junk to catch dangling refs.
    memset(v, 1, PGSIZE);

    if(kmem.use_lock)
      acquire(&kmem.lock);
    r = (struct run*)v;
    r->next = kmem.freelist;
    kmem.freelist = r;
    frees++;
    if(kmem.use_lock)
      release(&kmem.lock);
  }
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.

#include "proc.h"
#include "x86.h"

char*
kalloc(void)
{
  struct run *r;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
	  frees--;

    // Find index of page frame counter
    int i = ((uint)r - KERNBASE) / PGSIZE;
    pageframe_counters[i] = 1; // Set counter to 1
  }

  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}

int sys_frees(void)
{
	return frees;
}
