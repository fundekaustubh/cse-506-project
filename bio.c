#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

#define BUFFERCOUNT 5

struct
{
  struct spinlock lock;
  struct buf buf[BUFFERCOUNT];

  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head;
} bcache;

void binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // PAGEBREAK!
  //  Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for (b = bcache.buf; b < bcache.buf + BUFFERCOUNT; b++)
  {
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for (b = bcache.head.next; b != &bcache.head; b = b->next)
  {
    if (b->dev == dev && b->blockno == blockno)
    {
      cprintf("\nCached block found: %d for device number: %d", blockno, dev);
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached; recycle the least recently used buffer.
  // Here we are implementing FIFO, so we evict the first buffer.
  b = bcache.head.prev;
  cprintf("\nBlock not found: %d for device number: %d", blockno, dev);
  if (b->refcnt == 0 && (b->flags & B_DIRTY) == 0)
  {
    b->dev = dev;
    b->blockno = blockno;
    b->flags = 0;
    b->refcnt = 1;
    // Move the head to the next buffer
    bcache.head.prev = b->prev;
    b->prev->next = &bcache.head;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
    release(&bcache.lock);
    acquiresleep(&b->lock);
    return b;
  }

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if ((b->flags & B_VALID) == 0)
  {
    iderw(b);
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("bwrite");
  b->flags |= B_DIRTY;
  iderw(b);
}

// Release a locked buffer.
// Move to the head of the MRU list.
void brelse(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0)
  {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }

  release(&bcache.lock);
}
// PAGEBREAK!
//  Blank page.