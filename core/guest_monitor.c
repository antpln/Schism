#include "guest_stubs.h"
#include "guest_monitor.h"

extern void console_puts(const char*);
extern void console_hex64(u64);

void guest_shared_dump(void)
{
    console_puts("EL2: guest shared slots snapshot\n");
    for (u32 slot = 0; slot < GUEST_SHARED_SLOT_COUNT; ++slot)
    {
        volatile u64 *ptr = guest_shared_slot(slot);
        u64 value = *ptr;
        console_puts("  slot ");
        console_hex64(slot);
        console_puts(" @ ");
        console_hex64((u64)ptr);
        console_puts(" = ");
        console_hex64(value);
        console_puts("\n");
    }
}
