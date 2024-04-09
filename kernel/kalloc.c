// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// number of physical pages
#define PHYSICAL_PAGES_COUNT (PHYSTOP - KERNBASE) / PGSIZE

// given pa, get the index of reference count in the ref_array defined below 
#define REF_COUNT_IDX(pa) ((pa - KERNBASE) / PGSIZE)

int ref_count[PHYSICAL_PAGES_COUNT];
struct spinlock ref_count_lock;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);

  // init ref_count array as zero
  initlock(&ref_count_lock, "ref_count");
  for (uint64 i = 0; i < PHYSICAL_PAGES_COUNT; i++) {
    set_ref_count_via_idx(i, 0);
  }
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  if (get_ref_count((uint64)pa) == 0)
    goto free;

  add_ref_count((uint64)pa, -1);
  if (get_ref_count((uint64)pa) > 0) {
    return;
  }

free:
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
    set_ref_count_via_pa((uint64)r, 1);
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

void
add_ref_count(uint64 pa, int delta)
{
  if(pa < KERNBASE || pa >= PHYSTOP)
    panic("add_ref_count");

  acquire(&ref_count_lock);
  ref_count[REF_COUNT_IDX(pa)] += delta;
  release(&ref_count_lock);
}

void
set_ref_count_via_pa(uint64 pa, int new_count)
{
  if (pa < KERNBASE || pa >= PHYSTOP)
    panic("set_ref_count_via_pa");
  
  acquire(&ref_count_lock);
  ref_count[REF_COUNT_IDX(pa)] = new_count;
  release(&ref_count_lock);
}

void
set_ref_count_via_idx(uint64 idx, int new_count)
{
  if (idx >= PHYSICAL_PAGES_COUNT)
    panic("set_ref_count_via_idx");

  acquire(&ref_count_lock);
  ref_count[idx] = new_count;
  release(&ref_count_lock);
}


int
get_ref_count(uint64 pa)
{
  if (pa < KERNBASE || pa >= PHYSTOP)
    panic("get_ref_count");
  
  int count;
  acquire(&ref_count_lock);
  count = ref_count[REF_COUNT_IDX(pa)];
  release(&ref_count_lock);

  return count;
}
