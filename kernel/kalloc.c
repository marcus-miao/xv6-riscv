// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

uint64 freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct mem {
  struct spinlock lock;
  struct run *freelist;
  uint64 npages; // total number of physical pages (both used and free)
};

// struct {
//   struct spinlock lock;
//   struct run *freelist;
// } kmem;

struct mem kmem[NCPU]; 

struct spinlock steal;

void
kinit()
{
  const int BUF_SIZE = 10;
  char buf[BUF_SIZE];
  int id;

  initlock(&steal, "kmem_steal");

  // only cpu0 will enter kinit(). so it must init locks for other cpus as well
  for (id = 0; id < NCPU; id++) {
    // clear the buffer by setting all char to null
    memset(buf, 0, BUF_SIZE);
    snprintf(buf, BUF_SIZE, "kmem%d", id);
    initlock(&kmem[id].lock, buf);
    kmem[id].npages = 0;
    kmem[id].freelist = 0;
  }

  push_off();
  id = cpuid();
  pop_off();

  kmem[id].npages = freerange(end, (void*)PHYSTOP);
}

uint64
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  uint64 npages = 0;
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    kfree(p);
    npages++;
  }

  return npages;
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  int id; // cpuid
  push_off();
  id = cpuid();
  pop_off();

  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  int id; // cpuid
  push_off();
  id = cpuid();
  pop_off();

  struct run *r;

  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if (r) {
    kmem[id].freelist = r->next;
  } else {
    
    // avoid deadlock where two cpus try stealing other's freelist while holding
    // its own lock. adding this steal lock ensures that only one cpu can steal
    // at any given time.
    acquire(&steal);

    // try stealing. target amount of free space to steal is set to the same 
    // as the total number of physical pages used by this cpu. the underlying 
    // phylosophy here is the same as how to dynamically resize an array in Java.
    uint64 target = kmem[id].npages == 0 ? 1 : kmem[id].npages;
    uint64 stolen = 0;
    struct run *next;
    for (int otherid = 0; otherid < NCPU && stolen < target; otherid++) {
      if (otherid == id) continue; // don't steal from myself, avoid deadlock
      acquire(&kmem[otherid].lock);
      r = kmem[otherid].freelist; 
      while (r) {
        next = r->next;
        r->next = kmem[id].freelist;
        kmem[id].freelist = r;
        r = next;

        kmem[otherid].npages--;
        stolen++;
        if (stolen >= target) break;
      }
      kmem[otherid].freelist = r;
      release(&kmem[otherid].lock);
    }
    kmem[id].npages += stolen;

    r = kmem[id].freelist;
    if (r) {
      kmem[id].freelist = r->next;
    }

    release(&steal);
  }
  release(&kmem[id].lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
