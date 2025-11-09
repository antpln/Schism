#pragma once
#include "types.h"
static inline void mmio_write64(u64 addr, u64 v){ *(volatile u64*)(addr)=v; }
static inline void mmio_write32(u64 addr, u32 v){ *(volatile u32*)(addr)=v; }
static inline u32  mmio_read32 (u64 addr){ return *(volatile u32*)(addr); }
