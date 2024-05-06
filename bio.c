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

#define BUFFER_SIZE 5

struct
{
    struct spinlock lock;
    struct buf mbuf[MBUF];
    struct buf sbuf[SBUF];
    struct buf gbuf[GBUF];

    // Linked list of all buffers, through prev/next.
    // head.next is most recently used.
    struct buf mhead;
    struct buf shead;
    struct buf ghead;

} bcache;

void print_state()
{
    struct buf *b;
    cprintf("\nM: ");
    for (b = &bcache.mhead; b->next != &bcache.mhead; b = b->next)
    {
        cprintf("%d --", b->next->blockno);
    }
    cprintf("\nS: ");
    for (b = &bcache.shead; b->next != &bcache.shead; b = b->next)
    {
        cprintf("%d --", b->next->blockno);
    }
    cprintf("\nG: ");
    for (b = &bcache.ghead; b->next != &bcache.ghead; b = b->next)
    {
        cprintf("%d --", b->next->blockno);
    }
    cprintf("\n");
}
static struct buf *insert(uint blockno, uint dev);

void binit(void)
{
    struct buf *b;

    initlock(&bcache.lock, "bcache");

    // Create linked list of buffers for mbuf
    bcache.mhead.prev = &bcache.mhead;
    bcache.mhead.next = &bcache.mhead;
    for (b = bcache.mbuf; b < bcache.mbuf + MBUF; b++)
    {
        b->next = bcache.mhead.next;
        b->prev = &bcache.mhead;
        b->buf_type = 0;
        initsleeplock(&b->lock, "mbuffer");
        bcache.mhead.next->prev = b;
        bcache.mhead.next = b;
    }

    // Create linked list of buffers for gbuf
    bcache.shead.prev = &bcache.shead;
    bcache.shead.next = &bcache.shead;
    for (b = bcache.sbuf; b < bcache.sbuf + SBUF; b++)
    {
        b->next = bcache.shead.next;
        b->prev = &bcache.shead;
        b->buf_type = 1;
        initsleeplock(&b->lock, "sbuffer");
        bcache.shead.next->prev = b;
        bcache.shead.next = b;
    }

    // Create linked list of buffers for gbuf
    bcache.ghead.prev = &bcache.ghead;
    bcache.ghead.next = &bcache.ghead;
    for (b = bcache.gbuf; b < bcache.gbuf + GBUF; b++)
    {
        b->next = bcache.ghead.next;
        b->prev = &bcache.ghead;
        b->buf_type = 2;
        initsleeplock(&b->lock, "gbuffer");
        bcache.ghead.next->prev = b;
        bcache.ghead.next = b;
    }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
    cprintf("in bget\n");
    print_state();
    struct buf *b;

    acquire(&bcache.lock);
    // Is the block already cached?
    for (b = bcache.mhead.next; b != &bcache.mhead; b = b->next)
    {
        if (b->dev == dev && b->blockno == blockno)
        {
            b->refcnt++;
            release(&bcache.lock);
            acquiresleep(&b->lock);
            cprintf("Block already cached: %d\n", blockno);
            return b;
        }
    }
    for (b = bcache.shead.next; b != &bcache.shead; b = b->next)
    {
        if (b->dev == dev && b->blockno == blockno)
        {
            b->refcnt++;
            release(&bcache.lock);
            acquiresleep(&b->lock);
            cprintf("Block already cached: %d\n", blockno);
            return b;
        }
    }
    // b->blockno = blockno;
    // b->dev = dev;
    // b->refcnt += 1;
    // b->flags = 0;
    // release(&bcache.lock);
    // acquiresleep(&b->lock);
    // cprintf("No error yet\n");
    // return b;
    release(&bcache.lock);
    return insert(dev, blockno);
    panic("bget: no buffers");
}

static struct buf *
insert(uint dev, uint blockno)
{
    cprintf("In insert: dev: %d, blockno: %d\n", dev, blockno);
    struct buf *b;
    // try to put in main
    acquire(&bcache.lock);
    for (b = bcache.mhead.prev; b != &bcache.mhead; b = b->prev)
    {
        if (b->refcnt == 0 && (b->flags & B_DIRTY) == 0)
        {
            b->dev = dev;
            b->blockno = blockno;
            b->flags = 0;
            b->refcnt = 1;
            b->prev->next = b->next;
            b->next->prev = b->prev;

            // Move to head
            b->next = bcache.mhead.next;
            b->prev = &bcache.mhead;
            bcache.mhead.next->prev = b;
            bcache.mhead.next = b;
            release(&bcache.lock);
            acquiresleep(&b->lock);
            cprintf("Block not cached, newly allocated into MAIN buffer: %d\n", blockno);
            return b;
        }
    }

    cprintf("not space in mhead\n");
    for (b = bcache.shead.prev; b != &bcache.shead; b = b->prev)
    {
        cprintf("heyyyyyyy");
        if (b->refcnt == 0 && (b->flags & B_DIRTY) == 0)
        {
            b->dev = dev;
            b->blockno = blockno;
            b->flags = 0;
            b->refcnt = 1;
            b->prev->next = b->next;
            b->next->prev = b->prev;

            // Move to head
            b->next = bcache.shead.next;
            b->prev = &bcache.shead;
            bcache.shead.next->prev = b;
            bcache.shead.next = b;
            release(&bcache.lock);
            acquiresleep(&b->lock);
            cprintf("Block not cached, newly allocated into buffer: %d\n", blockno);
            return b;
        }
    }
    // try to put in small
    uint found_in_g = 0;
    for (b = bcache.ghead.next; !found_in_g && b != &bcache.ghead; b = b->next)
    {
        cprintf("dev: %d bno: %d needed: %d %d found_in_g: %d\n", b->dev, b->blockno, dev, blockno, found_in_g);
        if (b->dev == dev && b->blockno == blockno)
        {
            found_in_g = 1;
        }
    }
    if (found_in_g == 1)
    {
        // Insert into M
        cprintf("found in g\n");
        // acquire(&bcache.lock);
        b = bcache.mhead.prev;
        b->dev = dev;
        b->blockno = blockno;
        b->flags = 0;
        b->refcnt = 1;
        b->prev->next = b->next;
        b->next->prev = b->prev;

        // Move to head
        b->next = bcache.mhead.next;
        b->prev = &bcache.mhead;
        bcache.mhead.next->prev = b;
        bcache.mhead.next = b;
        release(&bcache.lock);
        acquiresleep(&b->lock);
        print_state();
        return b;
    }
    else
    {
        //
        cprintf("not found in g\n");
        // acquire(&bcache.lock);
        b = bcache.shead.prev;
        if (b->refcnt > 1)
        {
            // put into ghost
            struct buf *b1 = bcache.ghead.prev;
            b1->dev = b->dev;
            b1->blockno = b->dev;
            b1->refcnt = b->refcnt;
            b1->flags = b->flags;
            b1->prev->next = b1->next;
            b1->next->prev = b1->prev;

            // Move to head
            b1->next = bcache.ghead.next;
            b1->prev = &bcache.ghead;
            bcache.ghead.next->prev = b1;
            bcache.ghead.next = b1;
        }
        b->dev = dev;
        b->blockno = blockno;
        b->flags = 0;
        b->refcnt = 1;
        b->prev->next = b->next;
        b->next->prev = b->prev;

        // Move to head
        b->next = bcache.shead.next;
        b->prev = &bcache.shead;
        bcache.shead.next->prev = b;
        bcache.shead.next = b;
        release(&bcache.lock);
        acquiresleep(&b->lock);
        print_state();
        return b;
    }
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
    cprintf("bread\n");
    return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)
{
    if (!holdingsleep(&b->lock))
        panic("bwrite");
    b->flags |= B_DIRTY;
    iderw(b);
    cprintf("bwrite\n");
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
    release(&bcache.lock);
}
// PAGEBREAK!
//  Blank page.
