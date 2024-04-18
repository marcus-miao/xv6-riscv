// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// Number of hash buckets
#define NBUCKET 13

#define HASH(blockno) (blockno % NBUCKET)

struct {
  struct buf buf[NBUF];
  struct buf buckets[NBUCKET];
  struct spinlock bucketlock[NBUCKET];
  struct spinlock eviction_lock;
} bcache;

void
binit(void)
{
  struct buf *b;

  // bcache.buckets[i] is always a sentinel node
  for (int i = 0; i < NBUCKET; i++) {
    bcache.buckets[i].prev = &bcache.buckets[i];
    bcache.buckets[i].next = &bcache.buckets[i];
  }

  // Initially all buf is in the 0-th bucket since no buf has a valid blockno 
  for (b = bcache.buf; b < bcache.buf+NBUF; b++){
    // head insert
    b->next = bcache.buckets[0].next;
    b->prev = &bcache.buckets[0];
    initsleeplock(&b->lock, "buffer");
    bcache.buckets[0].next->prev = b;
    bcache.buckets[0].next = b;
  }

  struct spinlock *lock;
  for (lock = bcache.bucketlock; lock < bcache.bucketlock + NBUCKET; lock++) {
    initlock(lock, "bcache_bucket");
  }

  initlock(&bcache.eviction_lock, "bcache_eviction_lock");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  uint bucket_idx = HASH(blockno);
  acquire(&bcache.bucketlock[bucket_idx]);

  // Is the block already cached?
  for(b = bcache.buckets[bucket_idx].next; b != &bcache.buckets[bucket_idx]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucketlock[bucket_idx]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.bucketlock[bucket_idx]);

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.

  // ===========================================================================
  // Only one thread can enter the following section

  acquire(&bcache.eviction_lock);
  // printf("==== EVICTION ====\n");

  // It is possible that two threads are requesting the same blockno since 
  // bucket lock is released immediately after block cache check. To ensure
  // eviction happens exactly once, the following recheck is necessary
  acquire(&bcache.bucketlock[bucket_idx]);
  for (b = bcache.buckets[bucket_idx].next; b != &bcache.buckets[bucket_idx]; b = b->next) {
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucketlock[bucket_idx]);
      release(&bcache.eviction_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.bucketlock[bucket_idx]);

  uint min_timestamp = 0xffffffff;
  struct buf *curr;
  int holding_bucket_lock = -1;
  b = 0;
  for (int i = 0; i < NBUCKET; i++) {
    acquire(&bcache.bucketlock[i]);
    for (curr = bcache.buckets[i].prev; curr != &bcache.buckets[i]; curr = curr->prev) {
      if (curr->refcnt > 0)
        continue;
      if (curr->timestamp >= min_timestamp)
        continue;
      
      b = curr;
      min_timestamp = curr->timestamp;
      if (holding_bucket_lock >= 0 && holding_bucket_lock != i) {
        release(&bcache.bucketlock[holding_bucket_lock]);
      }
      holding_bucket_lock = i;
    }

    // If the current bucket contains lru unused buffer, hold the bucket lock 
    // until eviction is finished
    if (holding_bucket_lock != i) {
      release(&bcache.bucketlock[i]);
    }
  }

  if (holding_bucket_lock < 0)
    panic("bget: no buffers");

  b->dev = dev;
  b->blockno = blockno;
  b->valid = 0;
  b->refcnt = 1;

  b->next->prev = b->prev;
  b->prev->next = b->next;
  release(&bcache.bucketlock[holding_bucket_lock]);

  acquire(&bcache.bucketlock[bucket_idx]);
  curr = bcache.buckets[bucket_idx].next;
  while (curr != &bcache.buckets[bucket_idx] && curr->timestamp >= b->timestamp)
    curr = curr->next;
  
  if (curr == &bcache.buckets[bucket_idx])
    curr = curr->prev;
  b->prev = curr;
  b->next = curr->next;
  curr->next->prev = b;
  curr->next = b;
  release(&bcache.bucketlock[bucket_idx]);
  release(&bcache.eviction_lock);

  // Critical section end
  // ===========================================================================

  acquiresleep(&b->lock);
  return b;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int bucket_idx = HASH(b->blockno);
  acquire(&bcache.bucketlock[bucket_idx]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.buckets[bucket_idx].next;
    b->prev = &bcache.buckets[bucket_idx];
    bcache.buckets[bucket_idx].next->prev = b;
    bcache.buckets[bucket_idx].next = b;
    b->timestamp = ticks;
  }
  
  release(&bcache.bucketlock[bucket_idx]);
}

void
bpin(struct buf *b) {
  acquire(&bcache.bucketlock[HASH(b->blockno)]);
  b->refcnt++;
  release(&bcache.bucketlock[HASH(b->blockno)]);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.bucketlock[HASH(b->blockno)]);
  b->refcnt--;
  release(&bcache.bucketlock[HASH(b->blockno)]);
}


