#include "s2_mmu.h"
#include "types.h"
#include "platform.h"

#define S2_PT_ENTRIES   512
#define S2_PAGE_SIZE    0x1000ull
#define S2_PAGE_MASK    (~(S2_PAGE_SIZE - 1ull))
#define L1_SHIFT        30
#define L2_SHIFT        21
#define L3_SHIFT        12
#define LVL_INDEX_MASK  0x1ffull
#define S2_MAX_L2_TABLES 16
#define S2_MAX_L3_TABLES 1024

typedef struct s2_l3_table {
    u64 entries[S2_PT_ENTRIES];
} s2_l3_table_t;

typedef struct s2_l2_table {
    u64 entries[S2_PT_ENTRIES];
    s2_l3_table_t* children[S2_PT_ENTRIES];
} s2_l2_table_t;

static u64 s2_l1[S2_PT_ENTRIES] __attribute__((aligned(4096)));
static s2_l2_table_t s2_l2_pool[S2_MAX_L2_TABLES] __attribute__((aligned(4096)));
static s2_l3_table_t s2_l3_pool[S2_MAX_L3_TABLES] __attribute__((aligned(4096)));
static s2_l2_table_t* s2_l1_children[S2_PT_ENTRIES];
static u16 s2_l2_used;
static u16 s2_l3_used;

static inline uint64_t vtcr_el2_value(void)
{
    // VTCR_EL2 is the Stage-2 Translation Control Register for EL2.
    // VTCR_EL2 configuration:
    // - T0SZ=24 (40-bit IPA space)
    // - SL0=1 (starting level 1 for Stage-2)
    // - ORGN0=1, IRGN0=1 (Write-Back Read/Write Allocate - WBWA)
    // - SH0=3 (Inner Shareable)
    const uint64_t TG0_4K  = 0b00ull << 14;  // VTCR_EL2.TG0 -> 4KB granule for Stage-2
    const uint64_t SH0_IS  = 0b11ull << 12;  // VTCR_EL2.SH0 -> Inner Shareable
    const uint64_t ORGN0_WB= 0b1ull  << 10;  // VTCR_EL2.ORGN0 -> Outer WBWA
    const uint64_t IRGN0_WB= 0b1ull  << 8;   // VTCR_EL2.IRGN0 -> Inner WBWA
    const uint64_t SL0_L1  = 0b01ull << 6;   // VTCR_EL2.SL0 -> start walk at level 1
    const uint64_t PS_48   = 0b101ull<< 16;  // VTCR_EL2.PS  -> 48-bit physical address range
    const uint64_t T0SZ    = (64 - IPA_BITS); // VTCR_EL2.T0SZ -> IPA size (40 bits)
    return TG0_4K | SH0_IS | ORGN0_WB | IRGN0_WB | SL0_L1 | T0SZ | PS_48;
}

#define WR(reg, val) asm volatile("msr " reg ", %0" ::"r"(val) : "memory")

#define PA_48_MASK ((1ull << 48) - 1)

static inline u64 align_down(u64 val, u64 align)
{
    return val & ~(align - 1ull);
}

static inline u64 align_up(u64 val, u64 align)
{
    return (val + align - 1ull) & ~(align - 1ull);
}

static void s2_pt_panic(void)
{
    for (;;)
        asm volatile("wfi");
}

static void zero_qwords(u64* buf, u64 count)
{
    for (u64 i = 0; i < count; ++i)
        buf[i] = 0;
}

static void zero_l2_children(s2_l2_table_t* tbl)
{
    for (u64 i = 0; i < S2_PT_ENTRIES; ++i)
        tbl->children[i] = 0;
}

static void s2_tables_reset(void)
{
    zero_qwords(s2_l1, S2_PT_ENTRIES);
    for (u64 i = 0; i < S2_PT_ENTRIES; ++i)
        s2_l1_children[i] = 0;
    s2_l2_used = 0;
    s2_l3_used = 0;
}

static s2_l2_table_t* alloc_l2(void)
{
    if (s2_l2_used >= S2_MAX_L2_TABLES)
        s2_pt_panic();
    s2_l2_table_t* tbl = &s2_l2_pool[s2_l2_used++];
    zero_qwords(tbl->entries, S2_PT_ENTRIES);
    zero_l2_children(tbl);
    return tbl;
}

static s2_l3_table_t* alloc_l3(void)
{
    if (s2_l3_used >= S2_MAX_L3_TABLES)
        s2_pt_panic();
    s2_l3_table_t* tbl = &s2_l3_pool[s2_l3_used++];
    zero_qwords(tbl->entries, S2_PT_ENTRIES);
    return tbl;
}

static s2_l2_table_t* ensure_l2(u64 l1_idx)
{
    s2_l2_table_t* tbl = s2_l1_children[l1_idx];
    if (tbl)
        return tbl;

    tbl = alloc_l2();
    s2_l1_children[l1_idx] = tbl;
    u64 desc = ((u64)tbl & PA_48_MASK & S2_PAGE_MASK) | S2_TABLE;
    s2_l1[l1_idx] = desc;
    return tbl;
}

static s2_l3_table_t* ensure_l3(s2_l2_table_t* l2, u64 l2_idx)
{
    s2_l3_table_t* tbl = l2->children[l2_idx];
    if (tbl)
        return tbl;

    tbl = alloc_l3();
    l2->children[l2_idx] = tbl;
    l2->entries[l2_idx] = ((u64)tbl & PA_48_MASK & S2_PAGE_MASK) | S2_TABLE;
    return tbl;
}

static void s2_map_page(u64 ipa, u64 pa, u8 read, u8 write, u8 exec)
{
    u64 l1_idx = (ipa >> L1_SHIFT) & LVL_INDEX_MASK;
    u64 l2_idx = (ipa >> L2_SHIFT) & LVL_INDEX_MASK;
    u64 l3_idx = (ipa >> L3_SHIFT) & LVL_INDEX_MASK;

    s2_l2_table_t* l2 = ensure_l2(l1_idx);
    s2_l3_table_t* l3 = ensure_l3(l2, l2_idx);

    u64 desc = (pa & (PA_48_MASK & S2_PAGE_MASK)) |
               S2_PAGE |
               S2_AF |
               S2_SH_INNER |
               S2_MEMATTR(S2_ATTRIDX_NORMAL) |
               (read  ? S2AP_R : 0) |
               (write ? S2AP_W : 0);

    if (!exec)
        desc |= S2_XN;

    l3->entries[l3_idx] = desc;
}

static void s2_map_identity_range(u64 ipa_start, u64 pa_start, u64 size,
                                  u8 read, u8 write, u8 exec)
{
    if (!size)
        return;

    u64 map_start = align_down(ipa_start, S2_PAGE_SIZE);
    u64 map_end = align_up(ipa_start + size, S2_PAGE_SIZE);
    u64 pa = pa_start - (ipa_start - map_start);

    for (u64 cur = map_start; cur < map_end; cur += S2_PAGE_SIZE)
    {
        u64 cur_pa = pa + (cur - map_start);
        s2_map_page(cur, cur_pa, read, write, exec);
    }
}

void s2_build_tables_identity(u64 ipa, u64 pa, u64 vm_size, u32 vm_count,
                              u64 guard_bytes, u8 read, u8 write, u8 exec)
{
    if (!vm_count || !vm_size)
        return;

    guard_bytes = align_up(guard_bytes, S2_PAGE_SIZE);
    vm_size = align_up(vm_size, S2_PAGE_SIZE);

    s2_tables_reset();

    for (u32 vm = 0; vm < vm_count; ++vm)
    {
        u64 slot_offset = vm * (vm_size + guard_bytes);
        u64 slot_ipa = ipa + slot_offset;
        u64 slot_pa  = pa  + slot_offset;
        s2_map_identity_range(slot_ipa, slot_pa, vm_size, read, write, exec);
    }

    asm volatile("dsb ishst" ::: "memory");
}

void s2_program_regs_and_enable(void)
{
    WR("MAIR_EL2", MAIR_EL2_VALUE);   // Stage-2 memory attributes (AttrIndx -> Normal WBRWA / Device)
    WR("VTCR_EL2", vtcr_el2_value()); // Stage-2 translation control (granule/shareability/cacheability)
    const uint64_t VMID_SHIFT = 48;

    // Check CPU features to determine VMID size (8 or 16 bits)
    uint16_t mask;
    {
        uint64_t mmfr1;
        asm volatile("mrs %0, ID_AA64MMFR1_EL1" : "=r"(mmfr1));
        uint64_t vmidbits = (mmfr1 >> 4) & 0xF;     // VMIDBits
        mask = (vmidbits == 0x2) ? 0xFFFFu : 0xFFu; // FEAT_VMID16 ?
    }
    uint64_t vmid_field = ((uint64_t)(VMID & mask)) << VMID_SHIFT; // VMID -> [63:48]
    uintptr_t l1_base = (uintptr_t)s2_l1;                          // 4KB-aligned L1 table base
    uint64_t baddr_field = ((uint64_t)l1_base) & PA_48_MASK;       // Table base PA -> bits [47:0]

    uint64_t vttbr = vmid_field | baddr_field;                // Combine VMID and base address
    WR("VTTBR_EL2", vttbr);                                   // Stage-2 translation table base register
    asm volatile("dsb ish; tlbi vmalls12e1is; dsb ish; isb"); // Barrier + invalidate guest/host Stage-1/2 TLBs

    /*
     * HCR_EL2 (Hypervisor Configuration Register) controls virtualization at EL2.
     * It enables Stage-2 translation (VM), selects the guest execution state (RW),
     * and configures which guest operations trap to EL2 (e.g., TWI/TWE for WFI/WFE).
     * Additional fields route faults/interrupts and gate access to system features.
     * Below it is read, updated, and written back to enable S2 and desired traps.
     */

    uint64_t hcr;
    asm volatile("mrs %0, HCR_EL2" : "=r"(hcr)); // Read current HCR_EL2.
    uint64_t vm = (1ull << 0);                   // HCR_EL2.VM  -> enable Stage-2 translation
    uint64_t rw = (1ull << 31);                  // HCR_EL2.RW  -> force guest EL1 into AArch64
    uint64_t twe = (1ull << 14);                 // HCR_EL2.TWE -> trap guest WFE to EL2
    uint64_t twi = (1ull << 13);                 // HCR_EL2.TWI -> trap guest WFI to EL2
    uint64_t tsc = (1ull << 19);                 // HCR_EL2.TSC -> trap guest SMC instructions
    uint64_t fmo = (1ull << 3);                  // HCR_EL2.FMO -> route physical FIQs to EL2
    uint64_t imo = (1ull << 4);                  // HCR_EL2.IMO -> route physical IRQs to EL2
    uint64_t amo = (1ull << 5);                  // HCR_EL2.AMO -> route SError/async aborts to EL2
    hcr |= vm | rw | twe | twi | tsc | fmo | imo | amo;
    WR("HCR_EL2", hcr); // Enable Stage-2 translations with the selected traps
    asm volatile("dsb ish; tlbi vmalls12e1is; dsb ish; isb"); // Flush guest TLBs after toggling VM bit
}

// Helper function to determine VMID mask based on CPU features
static inline uint16_t vmid_mask_from_cpu(void)
{
    uint64_t mmfr1;
    asm volatile("mrs %0, ID_AA64MMFR1_EL1" : "=r"(mmfr1));
    uint64_t vmidbits = (mmfr1 >> 4) & 0xF;     // VMIDBits field
    return (vmidbits == 0x2) ? 0xFFFFu : 0xFFu; // 16 or 8 bits
}

// This function sets up the necessary registers and transitions from EL2 to EL1
void enter_el1_at(void (*el1_pc)(void), uint64_t sp_el1)
{

    asm volatile("msr SP_EL1, %0" ::"r"(sp_el1) : "memory"); // Program SP_EL1 for the guest
    const uint64_t EL1h = 0x5ull;         // SPSR_EL2.M bits -> return to EL1h
    const uint64_t DAIF = 0xFull << 6;    // SPSR_EL2.DAIF -> mask IRQ/FIQ/SError/Debug
    WR("SPSR_EL2", EL1h | DAIF);          // Save return state + interrupt mask
    WR("ELR_EL2", (uint64_t)el1_pc);      // Link register for eret -> guest entry PC
    asm volatile("isb; eret");            // Synchronize and drop to EL1
}
