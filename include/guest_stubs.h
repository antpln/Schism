#ifndef GUEST_STUBS_H
#define GUEST_STUBS_H

#include "types.h"
#include "guest_layout.h"

/*
 * The guest stubs run inside the same flat address space that starts at
 * 0x40000000. The layout constants above carve out regions inside that range so
 * the tiny OSes can exchange state or interact with virtual devices.
 */

static inline volatile u64* guest_shared_slot(u32 slot)
{
    return (volatile u64*)(GUEST_SHARED_BASE + (u64)slot * GUEST_SHARED_STRIDE);
}

static inline void guest_log_value(u32 slot, u64 value)
{
    *guest_shared_slot(slot) = value;
}

static inline void guest_yield(void)
{
    asm volatile("wfi" ::: "memory");
}


static inline void guest_delay(unsigned iterations)
{
    for (unsigned i = 0; i < iterations; ++i)
        asm volatile("nop");
}

static inline u64 guest_read_counter(void)
{
    u64 val;
    asm volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
}

static inline u64 guest_read_current_el(void)
{
    u64 val;
    asm volatile("mrs %0, CurrentEL" : "=r"(val));
    return val;
}

static inline u64 guest_read_sp(void)
{
    u64 val;
    asm volatile("mov %0, sp" : "=r"(val));
    return val;
}

static inline volatile u64* guest_private_region(u64 guest_id)
{
    return (volatile u64*)(GUEST_WORK_BASE + guest_id * GUEST_WORK_STRIDE);
}

static inline void guest_set_virtual_time(u64 virtual_cnt)
{
    register u64 x0 asm("x0") = virtual_cnt;
    asm volatile("hvc #0x61" : "+r"(x0) :: "memory");
}

extern void guest_counter_os(u64 guest_id);
extern void guest_memwalk_os(u64 guest_id);

#endif /* GUEST_STUBS_H */
