#pragma once
#include <stdint.h>

// Memory attribute encodings shared between EL2 Stage-1 and Stage-2.
#define MAIR_ATTR_NORMAL_WBWA  0xffu
#define MAIR_ATTR_DEVICE_nGnRE 0x04u

// MAIR entries referenced by AttrIndx fields in page table entries.
#define MAIR_EL2_VALUE ((u64)MAIR_ATTR_NORMAL_WBWA | \
                        ((u64)MAIR_ATTR_DEVICE_nGnRE << 8))

// AttrIndx values used in descriptors.
#define NORMAL_WB    0u
#define DEVICE_nGnRE 1u
