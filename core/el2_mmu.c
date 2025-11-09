#include "el2_mmu.h"

#define EL2_PT_ENTRIES 512
#define PAGE_SIZE      0x1000ull
#define PAGE_MASK      (~(PAGE_SIZE - 1ull))
#define L1_SHIFT       30          // Each L1 entry covers 1GB
#define L2_SHIFT       21          // Each L2 entry covers 2MB
#define L3_SHIFT       12          // Each L3 entry covers 4KB
#define LVL_INDEX_MASK 0x1ffull    // 9-bit index per level with 4KB granules
#define PA_48_MASK     ((1ull << 48) - 1ull) // Architected PA limit for QEMU virt

#define EL2_DESC_TABLE 0x3ull              // Table descriptors have [1:0]=11
#define EL2_PTE_PAGE   0x3ull              // L3 entries are also [1:0]=11
#define EL2_PTE_ATTR(x)   (((u64)(x) & 0x7ull) << 2) // AttrIndx -> MAIR_EL2 byte
#define EL2_PTE_SH_INNER  (0x3ull << 8)    // Inner-shareable so I/D caches stay coherent
#define EL2_PTE_AF        (1ull << 10)     // Access Flag must be set or we fault immediately
#define EL2_PTE_RDONLY    (1ull << 7)      // AP[2]=1 -> privileged read-only
#define EL2_PTE_PXN       (1ull << 53)     // Privileged Execute-Never
#define EL2_PTE_UXN       (1ull << 54)     // EL0 Execute-Never (belt-and-suspenders)

typedef struct el2_l3_table {
    u64 entries[EL2_PT_ENTRIES];
} el2_l3_table_t;

typedef struct el2_l2_table {
    u64 entries[EL2_PT_ENTRIES];
    el2_l3_table_t* children[EL2_PT_ENTRIES];
} el2_l2_table_t;

// Simple static pools for the few mappings we need at EL2. Each pool is 4KB-aligned
// so the hardware can consume the physical address directly.
static u64 el2_l1[EL2_PT_ENTRIES] __attribute__((aligned(4096)));
static el2_l2_table_t l2_pool[16] __attribute__((aligned(4096)));
static el2_l3_table_t l3_pool[64] __attribute__((aligned(4096)));
static el2_l2_table_t* l1_children[EL2_PT_ENTRIES];
static unsigned l2_used;
static unsigned l3_used;

// If we run out of page-table memory something is badly wrong; hang in place.
static void el2_pt_hang(void)
{
    for (;;)
        asm volatile("wfi");
}

static void zero_qword_array(u64* buf, unsigned count)
{
    for (unsigned i = 0; i < count; ++i)
        buf[i] = 0;
}

static void zero_l2_children(el2_l2_table_t* tbl)
{
    for (unsigned i = 0; i < EL2_PT_ENTRIES; ++i)
        tbl->children[i] = 0;
}

void el2_mmu_init(void)
{
    zero_qword_array(el2_l1, EL2_PT_ENTRIES);
    for (unsigned i = 0; i < EL2_PT_ENTRIES; ++i)
        l1_children[i] = 0;
    l2_used = 0;
    l3_used = 0;
}

static el2_l2_table_t* alloc_l2(void)
{
    if (l2_used >= (sizeof(l2_pool) / sizeof(l2_pool[0])))
        el2_pt_hang();
    el2_l2_table_t* tbl = &l2_pool[l2_used++];
    zero_qword_array(tbl->entries, EL2_PT_ENTRIES);
    zero_l2_children(tbl);
    return tbl;
}

static el2_l3_table_t* alloc_l3(void)
{
    if (l3_used >= (sizeof(l3_pool) / sizeof(l3_pool[0])))
        el2_pt_hang();
    el2_l3_table_t* tbl = &l3_pool[l3_used++];
    zero_qword_array(tbl->entries, EL2_PT_ENTRIES);
    return tbl;
}

// Ensure the L1 entry points at a valid L2 table so we can populate L3 pages.
static el2_l2_table_t* ensure_l2(u64 l1_idx)
{
    el2_l2_table_t* tbl = l1_children[l1_idx];
    if (tbl)
        return tbl;

    tbl = alloc_l2();
    l1_children[l1_idx] = tbl;

    u64 desc = ((u64)tbl & PAGE_MASK) | EL2_DESC_TABLE;
    el2_l1[l1_idx] = desc;
    return tbl;
}

// Stage-1 EL2 uses 4KB granules, so the terminal level is L3. Ensure we have one.
static el2_l3_table_t* ensure_l3(el2_l2_table_t* l2, u64 l2_idx)
{
    el2_l3_table_t* tbl = l2->children[l2_idx];
    if (tbl)
        return tbl;

    tbl = alloc_l3();
    l2->children[l2_idx] = tbl;
    l2->entries[l2_idx] = ((u64)tbl & PAGE_MASK) | EL2_DESC_TABLE;
    return tbl;
}

// Install one 4KB mapping using the requested attributes.
static void map_page(u64 va, u64 pa, u8 attr_idx, bool ro, bool exec)
{
    u64 l1_idx = (va >> L1_SHIFT) & LVL_INDEX_MASK;
    u64 l2_idx = (va >> L2_SHIFT) & LVL_INDEX_MASK;
    u64 l3_idx = (va >> L3_SHIFT) & LVL_INDEX_MASK;

    el2_l2_table_t* l2 = ensure_l2(l1_idx);
    el2_l3_table_t* l3 = ensure_l3(l2, l2_idx);

    u64 desc = (pa & (PA_48_MASK & PAGE_MASK)) |
               EL2_PTE_PAGE |
               EL2_PTE_ATTR(attr_idx) |
               EL2_PTE_SH_INNER |
               EL2_PTE_AF;

    if (ro)
        desc |= EL2_PTE_RDONLY;
    if (!exec)
        desc |= EL2_PTE_PXN | EL2_PTE_UXN;

    l3->entries[l3_idx] = desc;
}

void el2_map_range(u64 va_start, u64 pa_start, u64 size,
                   u8 attr_idx, bool ro, bool exec)
{
    if (!size)
        return;

    // Align the request down to 4KB so we can reuse map_page() for the edges.
    u64 offset = va_start & (PAGE_SIZE - 1ull);
    u64 va = va_start - offset;
    u64 pa = pa_start - offset;
    u64 end = va_start + size;
    u64 limit = (end + PAGE_SIZE - 1ull) & PAGE_MASK;

    for (u64 cur = va; cur < limit; cur += PAGE_SIZE)
    {
        u64 cur_pa = pa + (cur - va);
        map_page(cur, cur_pa, attr_idx, ro, exec);
    }
}

void el2_mmu_enable(void)
{
    asm volatile("dsb ishst" ::: "memory"); // Ensure page-table writes are visible

    // TTBR0_EL2 points at the root of the EL2 stage-1 walk; PA must be aligned.
    u64 ttbr0 = (u64)el2_l1 & (PA_48_MASK & PAGE_MASK);
    asm volatile("msr TTBR0_EL2, %0" : : "r"(ttbr0)); // Program Translation Table Base Register 0

    /*
     * TCR_EL2:
     *  - TG0=00 -> 4KB granule
     *  - SH0=11 -> inner-shareable
     *  - IRGN/ORGN=01 -> WBWA caches (matches MAIR encoding)
     *  - T0SZ=25 -> 64-25 = 39-bit VA space (maps our 512GB identity window)
     *  - IPS=101 -> 48-bit PARange (QEMU virt limit)
     */
    const u64 TG0_4K   = 0ull << 14;      // TCR_EL2.TG0
    const u64 SH0_INNER= 0b11ull << 12;   // TCR_EL2.SH0
    const u64 ORGN0_WB = 0b01ull << 10;   // TCR_EL2.ORGN0
    const u64 IRGN0_WB = 0b01ull << 8;    // TCR_EL2.IRGN0
    const u64 T0SZ     = 25ull;           // TCR_EL2.T0SZ (64 - VA bits)
    const u64 IPS_48   = 0b101ull << 16;  // TCR_EL2.IPS (physical address range)

    u64 tcr = T0SZ | TG0_4K | SH0_INNER | ORGN0_WB | IRGN0_WB | IPS_48;
    asm volatile("msr TCR_EL2, %0" : : "r"(tcr));              // Translation Control Register
    asm volatile("msr MAIR_EL2, %0" : : "r"(MAIR_EL2_VALUE)); // Memory Attribute Indirection Register

    asm volatile("dsb ish; isb" ::: "memory"); // Synchronize before enabling stage-1

    /*
     * SCTLR_EL2 bits we touch:
     *  - M (bit0) enables the MMU
     *  - C (bit2) turns on data cache for stage-1
     *  - I (bit12) turns on instruction cache
     * Everything else remains as set by early boot.
     */
    u64 sctlr;
    asm volatile("mrs %0, SCTLR_EL2" : "=r"(sctlr));  // Read System Control Register
    sctlr |= (1ull << 0)  |   // SCTLR_EL2.M   -> MMU enable
             (1ull << 2)  |   // SCTLR_EL2.C   -> data cache enable
             (1ull << 12);    // SCTLR_EL2.I   -> instruction cache enable
    asm volatile("msr SCTLR_EL2, %0" : : "r"(sctlr) : "memory"); // Write back updated control bits
    asm volatile("isb"); // Ensure subsequent instructions see enabled MMU/I-cache state
}
