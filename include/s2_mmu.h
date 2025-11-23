#pragma once
#include <stdint.h>
#include "mem_attrs.h"

// Number of bits used for the Intermediate Physical Address (IPA) in Stage-2
// translations. 39b IPA -> 512 GiB IPA space on the QEMU virt platform.
#define IPA_BITS 39 // IPA width used by Stage-2 (guest-physical) addresses

// VMID used in VTTBR_EL2 to tag Stage-2 translations for a given guest VM.
// Multiple guests can coexist by using different VMIDs to avoid TLB conflicts.
#define VMID 1      // Virtual Machine Identifier used in VTTBR_EL2.VMID

// Table/page/block common:
#define S2_DESC_VALID        (1ull << 0)          // Convenience alias for bit0
#define S2_BLOCK             (0b01ull)            // Block descriptor at level 1/2
#define S2_TABLE             (0b11ull)            // Table descriptor (points to next level)
#define S2_PAGE              (0b11ull)            // Level-3 page descriptor
#define S2_AF                (1ull << 10)         // Access Flag
#define S2_SH_INNER          (0b11ull << 8)       // Shareability
#define S2_MEMATTR(idx)      (((idx) & 7ull) << 2)// AttrIndx[2:0] -> MAIR_EL2
#define S2AP_R               (1ull << 6)          // S2AP[0] (read)
#define S2AP_W               (1ull << 7)          // S2AP[1] (write)
#define S2_XN                (1ull << 54)         // XN bit at [54] for S2 blocks/pages

// AttrIndx values we use when building Stage-2 entries (map to MAIR_EL2 bytes):
#define S2_ATTRIDX_NORMAL    NORMAL_WB    // AttrIndx 0 -> MAIR_EL2[7:0]  (Normal WB WA)
#define S2_ATTRIDX_DEVICE    DEVICE_nGnRE // AttrIndx 1 -> MAIR_EL2[15:8] (Device nGnRE)

#define S2_VM_GUARD_BYTES     (2ull * 0x1000ull) // 8KB guard between VM slots

void s2_build_tables_identity(uint64_t ipa_base, uint64_t pa_base, uint64_t vm_size,
                              uint32_t vm_count, uint64_t guard_bytes,
                              uint8_t read, uint8_t write, uint8_t exec);
void s2_program_regs_and_enable(void);
void enter_el1_at(void (*el1_pc)(void), uint64_t sp_el1);
