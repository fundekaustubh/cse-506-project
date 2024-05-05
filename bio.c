#include <stddef.h>
#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

#define BUFFER_SIZE 5

struct
{
	struct spinlock lock;
	struct buf buf[BUFFER_SIZE];

	// Linked list of all buffers, through prev/next.
	// head.next is most recently used.
	struct buf head;
} bcache;

void binit(void)
{
	struct buf *b;

	initlock(&bcache.lock, "bcache");

	// Create linked list of buffers
	bcache.head.prev = &bcache.head;
	bcache.head.next = &bcache.head;
	for (b = bcache.buf; b < bcache.buf + BUFFER_SIZE; b++)
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

	cprintf("\nCurrent buffer: ");
	for (b = bcache.head.next; b != &bcache.head; b = b->next)
	{
		cprintf("(%d, %d) ", b->blockno, b->frequency);
	}

	// Is the block already cached?
	for (b = bcache.head.next; b != &bcache.head; b = b->next)
	{
		if (b->dev == dev && b->blockno == blockno)
		{
			b->refcnt++;
			b->frequency++; // Increment frequency
			cprintf("\nCached block: %d", blockno);
			release(&bcache.lock);
			acquiresleep(&b->lock);
			return b;
		}
	}
	cprintf("\nBlock not found: %d", blockno);
	// Not cached; find least frequently used buffer
	struct buf *leastFrequent = NULL;
	for (b = bcache.head.next; b != &bcache.head; b = b->next)
	{
		if (leastFrequent == NULL || b->frequency < leastFrequent->frequency)
		{
			leastFrequent = b;
		}
	}

	if (leastFrequent == NULL)
	{
		panic("No buffers available");
	}

	cprintf("\nReplacement block: %d", leastFrequent->blockno);
	leastFrequent->dev = dev;
	leastFrequent->blockno = blockno;
	leastFrequent->flags = 0;
	leastFrequent->refcnt = 1;
	leastFrequent->frequency = 1; // Set frequency to 1 for newly allocated buffer
	release(&bcache.lock);
	acquiresleep(&leastFrequent->lock);
	return leastFrequent;
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