#pragma once
#include <stdbool.h>
#include "types.h"
#include "mem_attrs.h"

void el2_mmu_init(void);
void el2_map_range(u64 va_start, u64 pa_start, u64 size,
                   u8 attr_idx, bool ro, bool exec);
void el2_mmu_enable(void);
