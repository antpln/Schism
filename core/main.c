#include "types.h"
#include "platform.h"
#include "el2_mmu.h"
#include "s2_mmu.h"

extern void console_init(void);
extern void console_puts(const char*);
extern void console_hex64(u64);

extern u8 __text_start[], __text_end[];
extern u8 __rodata_start[], __rodata_end[];
extern u8 __data_start[], __data_end[];
extern u8 __bss_start[], __bss_end[];
extern u8 __stack_bottom[], __stack_top[];

static inline u64 read_CurrentEL(void){ u64 x; asm volatile("mrs %0, CurrentEL":"=r"(x)); return x; }

static void bss_clear(void){
    for (u8* p = __bss_start; p < __bss_end; ++p)
        *p = 0;
}

extern void el1_start(void);

void el2_main(void){
    bss_clear();
    console_init();
    console_puts("EL2: Hello from EL2!\n");

    el2_mmu_init();
    el2_map_range((u64)__text_start,  (u64)__text_start,
                  (u64)(__text_end   - __text_start),
                  NORMAL_WB, true,  true);

    el2_map_range((u64)__rodata_start,(u64)__rodata_start,
                  (u64)(__rodata_end - __rodata_start),
                  NORMAL_WB, true,  false);

    el2_map_range((u64)__data_start,  (u64)__data_start,
                  (u64)(__data_end   - __data_start),
                  NORMAL_WB, false, false);

    el2_map_range((u64)__bss_start,   (u64)__bss_start,
                  (u64)(__bss_end    - __bss_start),
                  NORMAL_WB, false, false);

    el2_map_range((u64)__stack_bottom, (u64)__stack_bottom,
                  (u64)(__stack_top   - __stack_bottom),
                  NORMAL_WB, false, false);

    el2_map_range(UART_PA, UART_PA, UART_SIZE,
                  DEVICE_nGnRE, false, false);

    el2_mmu_enable();
    console_puts("EL2: Stage-1 MMU enabled.\n");

    s2_build_tables_identity(0x40000000ull, 0x40000000ull,
                             0x40000000ull, 1, S2_VM_GUARD_BYTES,
                             1, 1, 1);
    console_puts("EL2: Stage-2 tables built.\n");

    s2_program_regs_and_enable();
    console_puts("EL2: Stage-2 MMU enabled.\n");

    enter_el1_at(el1_start, 0x40080000ull);
    console_puts("EL2: Transitioning to EL1...\n");

    for(;;) asm volatile("wfi");
}
