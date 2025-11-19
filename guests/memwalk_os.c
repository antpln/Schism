#include <stddef.h>
#include "guest_stubs.h"
#include "guest_tasks.h"

enum
{
    MEMWALK_SLOT_ID = 6,
    MEMWALK_SLOT_EL = 7,
    MEMWALK_SLOT_SP = 8,
    MEMWALK_SLOT_REGION = 9,
    MEMWALK_SLOT_CHECKSUM = 10,
    MEMWALK_SLOT_SEED = 11,
};

static void run_isolation_tests(u64 guest_id, volatile u64 *region)
{
    guest_log_value(MEMWALK_SLOT_ID, guest_id);
    guest_log_value(MEMWALK_SLOT_EL, guest_read_current_el());
    guest_log_value(MEMWALK_SLOT_SP, guest_read_sp());
    guest_log_value(MEMWALK_SLOT_REGION, (u64)region);

    region[0] = 0xBEEF0000ull | guest_id;
}

// A tiny guest OS that performs memory walk operations.
void guest_memwalk_os(u64 guest_id)
{
    volatile u64 *region = guest_private_region(guest_id);
    const size_t words = GUEST_WORK_SIZE / sizeof(u64);
    u64 seed = 0xfeed000000000000ull;

    run_isolation_tests(guest_id, region);

    while (1)
    {
        u64 checksum = 0;
        for (size_t i = 0; i < words; ++i)
        {
            u64 value = seed ^ ((u64)i << 8);
            region[i] = value;
            checksum ^= value;
        }

        guest_log_value(MEMWALK_SLOT_CHECKSUM, checksum);
        guest_log_value(MEMWALK_SLOT_SEED, seed);

        struct guest_task_result result;
        guest_task_memwalk(guest_id, &result);
        guest_task_report(guest_id, &result);
        
        seed += 0x111111111ull;
        guest_delay(200);
        guest_yield();
    }
}
