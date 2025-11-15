#include "guest_stubs.h"
#include "guest_tasks.h"

enum
{
    COUNTER_SLOT_ID = 0,
    COUNTER_SLOT_EL = 1,
    COUNTER_SLOT_SP = 2,
    COUNTER_SLOT_REGION = 3,
    COUNTER_SLOT_COUNTER = 4,
    COUNTER_SLOT_ITER = 5,
};

static void run_isolation_tests(u64 guest_id)
{
    guest_log_value(COUNTER_SLOT_ID, guest_id);
    guest_log_value(COUNTER_SLOT_EL, guest_read_current_el());
    guest_log_value(COUNTER_SLOT_SP, guest_read_sp());
    guest_log_value(COUNTER_SLOT_REGION, (u64)guest_private_region(guest_id));

    volatile u64 *region = guest_private_region(guest_id);
    const u64 pattern = 0xC0DE0000ull | guest_id;
    region[0] = pattern;
}

void guest_counter_os(u64 guest_id)
{
    run_isolation_tests(guest_id);

    struct guest_task_result result;
    u64 iteration = 0;
    while (1)
    {
        guest_task_counter(guest_id, &result);
        guest_log_value(COUNTER_SLOT_COUNTER, result.data0);
        guest_log_value(COUNTER_SLOT_ITER, iteration);

        //guest_task_report(guest_id, &result);

        iteration++;
        guest_delay(10000);
        guest_yield();
    }
}
