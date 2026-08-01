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

#define NBUCKET 13

struct {
  struct spinlock lock;           // serializes eviction
  struct buf buf[NBUF];

  // hash table: lock per bucket
  struct {
    struct spinlock lock;
    struct buf head;
  } bucket[NBUCKET];
} bcache;

static uint
bhash(uint dev, uint blockno)
{
  return (dev + blockno) % NBUCKET;
}

// Acquire one or two bucket locks in a consistent order to avoid deadlock.
static void
bacquire(uint i1, uint i2)
{
  if(i1 == i2){
    acquire(&bcache.bucket[i1].lock);
  } else if(i1 < i2){
    acquire(&bcache.bucket[i1].lock);
    acquire(&bcache.bucket[i2].lock);
  } else {
    acquire(&bcache.bucket[i2].lock);
    acquire(&bcache.bucket[i1].lock);
  }
}

static void
brelease(uint i1, uint i2)
{
  if(i1 == i2){
    release(&bcache.bucket[i1].lock);
  } else if(i1 < i2){
    release(&bcache.bucket[i2].lock);
    release(&bcache.bucket[i1].lock);
  } else {
    release(&bcache.bucket[i1].lock);
    release(&bcache.bucket[i2].lock);
  }
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  for(int i = 0; i < NBUCKET; i++){
    initlock(&bcache.bucket[i].lock, "bcache.bucket");
    bcache.bucket[i].head.prev = &bcache.bucket[i].head;
    bcache.bucket[i].head.next = &bcache.bucket[i].head;
  }

  // Initially put all buffers into bucket 0.
  for(b = bcache.buf; b < bcache.buf + NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    b->timestamp = 0;
    b->refcnt = 0;
    b->bucket = 0;
    b->next = bcache.bucket[0].head.next;
    b->prev = &bcache.bucket[0].head;
    bcache.bucket[0].head.next->prev = b;
    bcache.bucket[0].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  uint bi = bhash(dev, blockno);

  // Is the block already cached?
  acquire(&bcache.bucket[bi].lock);
  for(b = bcache.bucket[bi].head.next; b != &bcache.bucket[bi].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket[bi].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // Miss: drop bucket lock before taking bcache.lock (avoid deadlock).
  release(&bcache.bucket[bi].lock);

  // Serialize eviction.
  acquire(&bcache.lock);

  // Double-check: someone else may have installed the block.
  acquire(&bcache.bucket[bi].lock);
  for(b = bcache.bucket[bi].head.next; b != &bcache.bucket[bi].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket[bi].lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.bucket[bi].lock);

  // Find an unused LRU buffer and move it into bucket bi.
  // Only lock the victim's bucket and the destination bucket.
  for(;;){
    struct buf *victim = 0;
    for(b = bcache.buf; b < bcache.buf + NBUF; b++){
      if(b->refcnt == 0 && (victim == 0 || b->timestamp < victim->timestamp))
        victim = b;
    }
    if(victim == 0)
      panic("bget: no buffers");

    // Use recorded bucket membership (safe under bcache.lock: only eviction changes it).
    uint vbi = victim->bucket;

    bacquire(vbi, bi);

    // recheck under bucket locks
    if(victim->refcnt != 0 || victim->bucket != vbi){
      brelease(vbi, bi);
      continue;
    }

    // Remove from old bucket list.
    victim->next->prev = victim->prev;
    victim->prev->next = victim->next;

    // Install new identity.
    victim->dev = dev;
    victim->blockno = blockno;
    victim->valid = 0;
    victim->refcnt = 1;
    victim->timestamp = ticks;
    victim->bucket = bi;

    // Insert into destination bucket.
    victim->next = bcache.bucket[bi].head.next;
    victim->prev = &bcache.bucket[bi].head;
    bcache.bucket[bi].head.next->prev = victim;
    bcache.bucket[bi].head.next = victim;

    brelease(vbi, bi);
    release(&bcache.lock);

    acquiresleep(&victim->lock);
    return victim;
  }
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
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint bi = bhash(b->dev, b->blockno);
  acquire(&bcache.bucket[bi].lock);
  b->refcnt--;
  if(b->refcnt == 0){
    // record last-use time for LRU eviction; no global lock needed
    b->timestamp = ticks;
  }
  release(&bcache.bucket[bi].lock);
}

void
bpin(struct buf *b) {
  uint bi = bhash(b->dev, b->blockno);
  acquire(&bcache.bucket[bi].lock);
  b->refcnt++;
  release(&bcache.bucket[bi].lock);
}

void
bunpin(struct buf *b) {
  uint bi = bhash(b->dev, b->blockno);
  acquire(&bcache.bucket[bi].lock);
  b->refcnt--;
  release(&bcache.bucket[bi].lock);
}
