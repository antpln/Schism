#include <stddef.h>
#include "guest_tasks.h"

static void copy_desc(struct guest_task_result *out, const char *msg)
{
    size_t i = 0;
    for (; msg[i] && i + 1 < sizeof(out->desc); ++i)
        out->desc[i] = msg[i];
    out->desc[i] = '\0';
}

void guest_task_counter(u64 guest_id, struct guest_task_result *out)
{
    out->id = guest_id;
    const u64 sample_counter = guest_read_counter();
    out->data0 = sample_counter;
    out->data1 = (u64)guest_private_region(guest_id);
    copy_desc(out, "counter task");
}

void guest_task_memwalk(u64 guest_id, struct guest_task_result *out)
{
    volatile u64 *region = guest_private_region(guest_id);
    const size_t words = GUEST_WORK_SIZE / sizeof(u64);
    u64 checksum = 0;
    for (size_t i = 0; i < words; ++i)
    {
        u64 value = region[i] ^ ((u64)guest_id << 32);
        checksum ^= value;
    }
    out->id = guest_id;
    out->data0 = checksum;
    out->data1 = (u64)region;

    copy_desc(out, "memwalk task");
}

void guest_task_report(u64 guest_id, const struct guest_task_result *out)
{
    register uint64_t x0 asm("x0") = guest_id;
    register uint64_t x1 asm("x1") = (uint64_t)out;
    asm volatile("hvc #0x60" : "+r"(x0), "+r"(x1) :: "memory");
}
