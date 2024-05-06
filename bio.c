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
//
// The implementation uses two state flags internally:
// * B_VALID: the buffer data has been read from the disk.
// * B_DIRTY: the buffer data has been modified
//     and needs to be written to disk.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

struct
{
    struct spinlock lock;
    struct buf buf[MBUF];
    struct buf gbuf[GBUF];

    // Linked list of all buffers, through prev/next.
    // head.next is most recently used.
    struct buf head;
    struct buf ghead;

} bcache;

void printBcacheBlocks(void)
{
    // struct buf *b;

    // //cprintf("Block numbers in bcache: \n");
    // //cprintf("\nM: ");
    // for (b = &bcache.head; b->next != &bcache.head; b = b->next)
    // {
    //     //cprintf("%d --", b->next->blockno);
    // }
    // //cprintf("\nS: ");
    // for (b = &bcache.ghead; b->next != &bcache.ghead; b = b->next)
    // {
    //     //cprintf("%d --", b->next->blockno);
    // }
    // //cprintf("\n"); // End the line after printing all block numbers
}

void binit(void)
{
    struct buf *b;

    initlock(&bcache.lock, "bcache");

    // PAGEBREAK!
    //  Create linked list of buffers
    bcache.head.prev = &bcache.head;
    bcache.head.next = &bcache.head;
    for (b = bcache.buf; b < bcache.buf + MBUF; b++)
    {
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        initsleeplock(&b->lock, "buffer");
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }
    bcache.ghead.prev = &bcache.ghead;
    bcache.ghead.next = &bcache.ghead;
    for (b = bcache.gbuf; b < bcache.gbuf + GBUF; b++)
    {
        b->next = bcache.ghead.next;
        b->prev = &bcache.ghead;
        initsleeplock(&b->lock, "buffer");
        bcache.ghead.next->prev = b;
        bcache.ghead.next = b;
    }
    //cprintf("---After initializing--- \n");
    printBcacheBlocks();
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
// Helper function to move a buffer to the head of the list
static void
movetohead(struct buf *b, int p)
{
    // Remove from current position
    if (p == 0)
    {
        b->prev->next = b->next;
        b->next->prev = b->prev;

        // Move to head
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }
    else
    {
        b->prev->next = b->next;
        b->next->prev = b->prev;

        // Move to head
        b->next = bcache.ghead.next;
        b->prev = &bcache.ghead;
        bcache.ghead.next->prev = b;
        bcache.ghead.next = b;
    }
}
static struct buf *
bget(uint dev, uint blockno)
{
    //cprintf("Get for dev: %d blockno: %d", dev, blockno);
    struct buf *b;

    acquire(&bcache.lock);
    // Is the block already cached?
    for (b = bcache.head.next; b != &bcache.head; b = b->next)
    {
        if (b->dev == dev && b->blockno == blockno)
        {
            b->refcnt++;
            //   movetohead(b); // Move to the head of the list since it's used
            release(&bcache.lock);
            acquiresleep(&b->lock);
            // //cprintf("---During Bget for block: %d--- \n", blockno);
            printBcacheBlocks();
            return b;
        }
    }
    // if block in ghost queue, then bring it back to front of normal queue
    for (b = bcache.ghead.next; b != &bcache.ghead; b = b->next)
    {
        if (b->dev == dev && b->blockno == blockno)
        {
            //cprintf("\nBringing back to main queue");
            b->refcnt++;
            // movetohead(b1, 0);
            release(&bcache.lock);
            acquiresleep(&b->lock);
            printBcacheBlocks();
            return b;
        }
    }

    // Not cached; recycle an unused buffer.
    // Even if refcnt==0, B_DIRTY indicates a buffer is in use
    // because log.c has modified it but not yet committed it.
    for (b = bcache.head.prev; b != &bcache.head; b = b->prev)
    {
        if (b->refcnt == 0 && (b->flags & B_DIRTY) == 0)
        {
            b->dev = dev;
            b->blockno = blockno;
            b->flags = 0;
            b->refcnt = 1;
            movetohead(b, 0); // Move to the head of the list since it's newly allocated
            release(&bcache.lock);
            acquiresleep(&b->lock);
            // //cprintf("---During Bget for block: %d--- \n", blockno);
            printBcacheBlocks();
            return b;
        }
    }

    // If no unused buffer is found, forcefully evict the last buffer
    b = bcache.head.prev;
    if (b->refcnt >= 3)
    {
        //cprintf("Added to ghost!\n");
        struct buf *b1 = bcache.ghead.prev;
        b1->dev = b->dev;
        b1->blockno = b->blockno;
        b1->flags = 0;
        b1->refcnt = 1;
        movetohead(b1, 1);
    }
    b->dev = dev;
    b->blockno = blockno;
    b->flags = 0;
    b->refcnt = 1;
    movetohead(b, 0); // Move to the head of the list since it's newly allocated
    release(&bcache.lock);
    acquiresleep(&b->lock);
    // //cprintf("Block not cached, evicting least recently used buffer and allocating: %d\n", blockno);
    // //cprintf("---During Bget for block: %d--- \n", blockno);
    printBcacheBlocks();
    return b;
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

    // Move to the head of the MRU list
    //   b->next->prev = b->prev;
    //   b->prev->next = b->next;
    //   b->next = bcache.head.next;
    //   b->prev = &bcache.head;
    //   bcache.head.next->prev = b;
    //   bcache.head.next = b;
    release(&bcache.lock);
    // //cprintf("---During Brelse--- \n");
    printBcacheBlocks();
}

// PAGEBREAK!
//  Blank page.
