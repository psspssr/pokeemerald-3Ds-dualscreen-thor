/*
 * The emulated linear heap and VRAM, and the region table (ctrshim_mem.h).
 *
 * Space is accounted with libctru's MemPool (allocator/mem_pool.cpp, zlib
 * licence, devkitPro), ported to C, over the virtual address ranges the
 * console uses: first fit, sizes padded to the alignment, coalescing on free,
 * VRAM as two 3 MiB banks. That is what decides whether an allocation
 * succeeds and what *SpaceFree reports, so origin sees the same failures and
 * fragmentation as on hardware. The memory itself is an ordinary aligned
 * heap block per allocation.
 */
#include <3ds/allocator/linear.h>
#include <3ds/allocator/mappable.h>
#include <3ds/allocator/vram.h>
#include <3ds/os.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "ctrshim_mem.h"
#include "shim_internal.h"

#define DEFAULT_LINEAR_HEAP (32u * 1024u * 1024u)
/* Large blocks get page alignment; it costs nothing at that size. */
#define PAGE_ALIGN_FROM (64u * 1024u)

extern u32 __ctru_linear_heap_size;

typedef struct MemBlock
{
    struct MemBlock *prev, *next;
    u32 base;
    u32 size;
} MemBlock;

typedef struct
{
    MemBlock *first, *last;
} MemPool;

typedef struct
{
    u32 addr;
    u32 size;
} MemChunk;

enum
{
    POOL_NONE,
    POOL_LINEAR,
    POOL_VRAM_A,
    POOL_VRAM_B,
};

typedef struct
{
    CtrMemBlock block;
    int pool;
    size_t poolSize;
    u32 vaddr;
} Entry;

static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
static MemPool sLinearPool, sVramPoolA, sVramPoolB;
static bool sLinearReady, sVramReady;
static Entry *sEntries;
static size_t sCount, sCapacity;

/* ── libctru's MemPool ─────────────────────────────────────────────────── */

static int AlignmentToShift(size_t alignment)
{
    if (alignment < 16)
        alignment = 16;
    else if (alignment & (alignment - 1))
        return -1;
    return __builtin_ffs((int)alignment) - 1;
}

static MemBlock *BlockCreate(u32 base, u32 size)
{
    MemBlock *b = malloc(sizeof(*b));

    if (b == NULL)
        return NULL;
    b->prev = b->next = NULL;
    b->base = base;
    b->size = size;
    return b;
}

static void PoolAddBlock(MemPool *pool, MemBlock *blk)
{
    blk->prev = pool->last;
    if (pool->last)
        pool->last->next = blk;
    if (!pool->first)
        pool->first = blk;
    pool->last = blk;
}

static void PoolDelBlock(MemPool *pool, MemBlock *b)
{
    MemBlock *prev = b->prev, *next = b->next;

    if (prev)
        prev->next = next;
    else
        pool->first = next;
    if (next)
        next->prev = prev;
    else
        pool->last = prev;
    free(b);
}

static void PoolInsertBefore(MemPool *pool, MemBlock *b, MemBlock *p)
{
    MemBlock *prev = b->prev;

    if (prev)
        prev->next = p;
    else
        pool->first = p;
    b->prev = p;
    p->next = b;
    p->prev = prev;
}

static void PoolInsertAfter(MemPool *pool, MemBlock *b, MemBlock *n)
{
    MemBlock *next = b->next;

    if (next)
        next->prev = n;
    else
        pool->last = n;
    b->next = n;
    n->prev = b;
    n->next = next;
}

static void PoolCoalesceRight(MemPool *pool, MemBlock *b)
{
    u32 curPtr = b->base + b->size;
    MemBlock *next;

    for (MemBlock *n = b->next; n; n = next)
    {
        next = n->next;
        if (n->base != curPtr)
            break;
        b->size += n->size;
        curPtr += n->size;
        PoolDelBlock(pool, n);
    }
}

static bool PoolAllocate(MemPool *pool, MemChunk *chunk, u32 size, int align)
{
    u32 alignMask;

    if (align >= 32 || align <= 0)
        return false;
    alignMask = (1u << align) - 1;
    if (size & alignMask)
    {
        if (size > UINT32_MAX - alignMask)
            return false;
        size = (size + alignMask) & ~alignMask;
    }
    for (MemBlock *b = pool->first; b; b = b->next)
    {
        u32 addr = b->base;
        u32 begWaste = addr & alignMask;
        u32 bSize;

        if (begWaste > 0)
            begWaste = alignMask + 1 - begWaste;
        if (begWaste > b->size)
            continue;
        addr += begWaste;
        bSize = b->size - begWaste;
        if (bSize < size)
            continue;

        chunk->addr = addr;
        chunk->size = size;
        if (!begWaste)
        {
            b->base += size;
            b->size -= size;
            if (!b->size)
                PoolDelBlock(pool, b);
        }
        else
        {
            u32 nAddr = addr + size;
            u32 nSize = bSize - size;

            b->size = begWaste;
            if (nSize)
            {
                MemBlock *n = BlockCreate(nAddr, nSize);

                if (n)
                    PoolInsertAfter(pool, b, n);
                else
                    chunk->size += nSize;
            }
        }
        return true;
    }
    return false;
}

static void PoolDeallocate(MemPool *pool, const MemChunk *chunk)
{
    u32 cAddr = chunk->addr, cSize = chunk->size;
    bool done = false;

    for (MemBlock *b = pool->first; !done && b; b = b->next)
    {
        if (b->base > cAddr)
        {
            if (cAddr + cSize == b->base)
            {
                b->base = cAddr;
                b->size += cSize;
            }
            else
            {
                MemBlock *c = BlockCreate(cAddr, cSize);

                if (c)
                    PoolInsertBefore(pool, b, c);
            }
            done = true;
        }
        else if (b->base + b->size == cAddr)
        {
            b->size += cSize;
            PoolCoalesceRight(pool, b);
            done = true;
        }
    }
    if (!done)
    {
        MemBlock *b = BlockCreate(cAddr, cSize);

        if (b)
            PoolAddBlock(pool, b);
    }
}

static u32 PoolFreeSpace(const MemPool *pool)
{
    u32 acc = 0;

    for (const MemBlock *b = pool->first; b; b = b->next)
        acc += b->size;
    return acc;
}

/* ── The region table ──────────────────────────────────────────────────── */

/* The first entry whose base is >= base. */
static size_t LowerBound(uintptr_t base)
{
    size_t lo = 0, hi = sCount;

    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;

        if ((uintptr_t)sEntries[mid].block.base < base)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/* The entry containing ptr in its current label, or NULL. */
static Entry *Containing(const void *ptr)
{
    uintptr_t p = (uintptr_t)ptr;
    size_t i = LowerBound(p + 1);
    Entry *e;

    if (i == 0)
        return NULL;
    e = &sEntries[i - 1];
    return p - (uintptr_t)e->block.base < e->block.size ? e : NULL;
}

static Entry *Exact(const void *base)
{
    size_t i = LowerBound((uintptr_t)base);

    return i < sCount && sEntries[i].block.base == base ? &sEntries[i] : NULL;
}

/* Would [base, base + size) overlap any entry other than the one at skip? */
static bool Overlaps(uintptr_t base, size_t size, size_t at, const Entry *skip)
{
    if (at > 0)
    {
        const Entry *prev = &sEntries[at - 1];

        if (prev != skip && (uintptr_t)prev->block.base + prev->block.size > base)
            return true;
    }
    for (size_t i = at; i < sCount; ++i)
    {
        if (&sEntries[i] == skip)
            continue;
        return (uintptr_t)sEntries[i].block.base < base + size;
    }
    return false;
}

static bool Insert(const Entry *entry)
{
    size_t at = LowerBound((uintptr_t)entry->block.base);

    if (Overlaps((uintptr_t)entry->block.base, entry->block.size, at, NULL))
        return false;
    if (sCount == sCapacity)
    {
        size_t capacity = sCapacity ? sCapacity * 2 : 256;
        Entry *grown = realloc(sEntries, capacity * sizeof(*grown));

        if (grown == NULL)
            return false;
        sEntries = grown;
        sCapacity = capacity;
    }
    memmove(&sEntries[at + 1], &sEntries[at], (sCount - at) * sizeof(*sEntries));
    sEntries[at] = *entry;
    ++sCount;
    return true;
}

static void Remove(Entry *entry)
{
    size_t at = (size_t)(entry - sEntries);

    memmove(&sEntries[at], &sEntries[at + 1], (sCount - at - 1) * sizeof(*sEntries));
    --sCount;
}

static CtrMemKind PoolKind(int pool)
{
    return pool == POOL_LINEAR ? CTR_MEM_LINEAR : pool == POOL_NONE ? CTR_MEM_NONE : CTR_MEM_VRAM;
}

void CtrMem_Register(CtrMemKind kind, void *base, size_t size, void *owner)
{
    Entry *existing;
    bool ok = true;

    if (base == NULL)
        return;
    pthread_mutex_lock(&sLock);
    existing = Exact(base);
    if (existing != NULL)
    {
        size_t at = (size_t)(existing - sEntries);

        if (Overlaps((uintptr_t)base, size, at, existing))
            ok = false;
        else
        {
            existing->block.kind = kind;
            existing->block.size = size;
            existing->block.owner = owner;
        }
    }
    else
    {
        Entry entry = {{kind, base, size, owner}, POOL_NONE, 0, 0};

        ok = Insert(&entry);
    }
    pthread_mutex_unlock(&sLock);
    if (!ok)
        ShimLogf(ANDROID_LOG_ERROR, "CtrMem_Register(%d, %p, %zu): overlaps another block", (int)kind, base,
                 size);
}

void CtrMem_Unregister(void *base)
{
    Entry *entry;

    pthread_mutex_lock(&sLock);
    entry = Exact(base);
    if (entry != NULL)
    {
        if (entry->pool != POOL_NONE)
        {
            entry->block.kind = PoolKind(entry->pool);
            entry->block.size = entry->poolSize;
            entry->block.owner = NULL;
        }
        else
            Remove(entry);
    }
    pthread_mutex_unlock(&sLock);
}

bool CtrMem_Find(const void *ptr, CtrMemBlock *out)
{
    Entry *entry;

    pthread_mutex_lock(&sLock);
    entry = Containing(ptr);
    if (entry != NULL && out != NULL)
        *out = entry->block;
    pthread_mutex_unlock(&sLock);
    return entry != NULL;
}

void CtrMem_SetOwner(void *base, void *owner)
{
    Entry *entry;

    pthread_mutex_lock(&sLock);
    entry = Exact(base);
    if (entry != NULL)
        entry->block.owner = owner;
    pthread_mutex_unlock(&sLock);
}

size_t CtrMem_Used(CtrMemKind kind)
{
    size_t used = 0;

    pthread_mutex_lock(&sLock);
    for (size_t i = 0; i < sCount; ++i)
        if (sEntries[i].block.kind == kind)
            used += sEntries[i].block.size;
    pthread_mutex_unlock(&sLock);
    return used;
}

/* ── linearAlloc / vramAlloc ───────────────────────────────────────────── */

static MemPool *Pool(int pool)
{
    switch (pool)
    {
    case POOL_LINEAR: return &sLinearPool;
    case POOL_VRAM_A: return &sVramPoolA;
    case POOL_VRAM_B: return &sVramPoolB;
    default: return NULL;
    }
}

static bool LinearInit(void)
{
    u32 size = __ctru_linear_heap_size ? __ctru_linear_heap_size : DEFAULT_LINEAR_HEAP;
    MemBlock *blk;

    if (sLinearReady)
        return true;
    blk = BlockCreate(OS_FCRAM_VADDR, size & ~0xFFFu);
    if (blk == NULL)
        return false;
    PoolAddBlock(&sLinearPool, blk);
    sLinearReady = true;
    return true;
}

static bool VramInit(void)
{
    MemBlock *a, *b;

    if (sVramReady)
        return true;
    a = BlockCreate(OS_VRAM_VADDR, OS_VRAM_SIZE / 2);
    b = BlockCreate(OS_VRAM_VADDR + OS_VRAM_SIZE / 2, OS_VRAM_SIZE / 2);
    if (a == NULL || b == NULL)
    {
        free(a);
        free(b);
        return false;
    }
    PoolAddBlock(&sVramPoolA, a);
    PoolAddBlock(&sVramPoolB, b);
    sVramReady = true;
    return true;
}

/* Called with sLock held, after the pool has given out chunk. */
static void *Back(int pool, const MemChunk *chunk, int shift)
{
    size_t align = (size_t)1 << shift;
    void *memory = NULL;
    Entry entry;

    if (align < 0x80)
        align = 0x80;
    if (chunk->size >= PAGE_ALIGN_FROM && align < 4096)
        align = 4096;
    if (posix_memalign(&memory, align, chunk->size) != 0)
        memory = NULL;
    if (memory != NULL)
    {
        memset(memory, 0, chunk->size);
        entry.block.kind = PoolKind(pool);
        entry.block.base = memory;
        entry.block.size = chunk->size;
        entry.block.owner = NULL;
        entry.pool = pool;
        entry.poolSize = chunk->size;
        entry.vaddr = chunk->addr;
        if (!Insert(&entry))
        {
            free(memory);
            memory = NULL;
        }
    }
    if (memory == NULL)
        PoolDeallocate(Pool(pool), chunk);
    return memory;
}

static u32 RequestSize(size_t size)
{
    if (size == 0)
        return 1;
    return size > UINT32_MAX ? 0 : (u32)size;
}

void *linearMemAlign(size_t size, size_t alignment)
{
    int shift = AlignmentToShift(alignment);
    u32 request = RequestSize(size);
    MemChunk chunk;
    void *memory = NULL;

    if (shift < 0 || request == 0)
        return NULL;
    pthread_mutex_lock(&sLock);
    if (LinearInit() && PoolAllocate(&sLinearPool, &chunk, request, shift))
        memory = Back(POOL_LINEAR, &chunk, shift);
    pthread_mutex_unlock(&sLock);
    return memory;
}

void *linearAlloc(size_t size)
{
    return linearMemAlign(size, 0x80);
}

void *linearRealloc(void *mem, size_t size)
{
    (void)mem;
    (void)size;
    return NULL;
}

static size_t PoolBlockSize(void *mem, bool linear)
{
    Entry *entry;
    size_t size = 0;

    pthread_mutex_lock(&sLock);
    entry = Exact(mem);
    if (entry != NULL && (linear ? entry->pool == POOL_LINEAR : entry->pool >= POOL_VRAM_A))
        size = entry->poolSize;
    pthread_mutex_unlock(&sLock);
    return size;
}

static void PoolFree(void *mem, bool linear)
{
    Entry *entry;
    void *memory = NULL;

    pthread_mutex_lock(&sLock);
    entry = Exact(mem);
    if (entry != NULL && (linear ? entry->pool == POOL_LINEAR : entry->pool >= POOL_VRAM_A))
    {
        MemChunk chunk = {entry->vaddr, (u32)entry->poolSize};

        PoolDeallocate(Pool(entry->pool), &chunk);
        memory = entry->block.base;
        Remove(entry);
    }
    pthread_mutex_unlock(&sLock);
    free(memory);
}

size_t linearGetSize(void *mem)
{
    return PoolBlockSize(mem, true);
}

void linearFree(void *mem)
{
    PoolFree(mem, true);
}

u32 linearSpaceFree(void)
{
    u32 space;

    pthread_mutex_lock(&sLock);
    space = LinearInit() ? PoolFreeSpace(&sLinearPool) : 0;
    pthread_mutex_unlock(&sLock);
    return space;
}

void *vramMemAlignAt(size_t size, size_t alignment, vramAllocPos pos)
{
    int shift = AlignmentToShift(alignment);
    u32 request = RequestSize(size);
    MemChunk chunk;
    int pool = POOL_NONE;
    void *memory = NULL;

    if (shift < 0 || request == 0)
        return NULL;
    pthread_mutex_lock(&sLock);
    if (VramInit())
    {
        switch (pos & VRAM_ALLOC_ANY)
        {
        case VRAM_ALLOC_A:
            if (PoolAllocate(&sVramPoolA, &chunk, request, shift))
                pool = POOL_VRAM_A;
            break;
        case VRAM_ALLOC_B:
            if (PoolAllocate(&sVramPoolB, &chunk, request, shift))
                pool = POOL_VRAM_B;
            break;
        case VRAM_ALLOC_ANY:
        {
            bool preferA = PoolFreeSpace(&sVramPoolA) >= PoolFreeSpace(&sVramPoolB);
            int first = preferA ? POOL_VRAM_A : POOL_VRAM_B;
            int second = preferA ? POOL_VRAM_B : POOL_VRAM_A;

            if (PoolAllocate(Pool(first), &chunk, request, shift))
                pool = first;
            else if (PoolAllocate(Pool(second), &chunk, request, shift))
                pool = second;
            break;
        }
        default:
            break;
        }
        if (pool != POOL_NONE)
            memory = Back(pool, &chunk, shift);
    }
    pthread_mutex_unlock(&sLock);
    return memory;
}

void *vramMemAlign(size_t size, size_t alignment)
{
    return vramMemAlignAt(size, alignment, VRAM_ALLOC_ANY);
}

void *vramAllocAt(size_t size, vramAllocPos pos)
{
    return vramMemAlignAt(size, 0x80, pos);
}

void *vramAlloc(size_t size)
{
    return vramMemAlignAt(size, 0x80, VRAM_ALLOC_ANY);
}

void *vramRealloc(void *mem, size_t size)
{
    (void)mem;
    (void)size;
    return NULL;
}

size_t vramGetSize(void *mem)
{
    return PoolBlockSize(mem, false);
}

void vramFree(void *mem)
{
    PoolFree(mem, false);
}

u32 vramSpaceFree(void)
{
    u32 space;

    pthread_mutex_lock(&sLock);
    space = VramInit() ? PoolFreeSpace(&sVramPoolA) + PoolFreeSpace(&sVramPoolB) : 0;
    pthread_mutex_unlock(&sLock);
    return space;
}

u32 osConvertVirtToPhys(const void *vaddr)
{
    uintptr_t p = (uintptr_t)vaddr;
    u32 phys = 0;
    size_t i;

    pthread_mutex_lock(&sLock);
    i = LowerBound(p + 1);
    if (i > 0)
    {
        const Entry *e = &sEntries[i - 1];
        uintptr_t offset = p - (uintptr_t)e->block.base;

        if (e->pool == POOL_LINEAR && offset < e->poolSize)
            phys = OS_FCRAM_PADDR + (e->vaddr - OS_FCRAM_VADDR) + (u32)offset;
        else if (e->pool >= POOL_VRAM_A && offset < e->poolSize)
            phys = OS_VRAM_PADDR + (e->vaddr - OS_VRAM_VADDR) + (u32)offset;
    }
    pthread_mutex_unlock(&sLock);
    return phys;
}

void mappableInit(u32 addrMin, u32 addrMax)
{
    (void)addrMin;
    (void)addrMax;
}
