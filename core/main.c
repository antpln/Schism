#include "types.h"
#include <stddef.h>
#include "platform.h"
#include "el2_mmu.h"
#include "s2_mmu.h"
#include "vcpu.h"
#include "guest_stubs.h"

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

static vcpu_t vcpu_pool[2];

static void memclr(void* ptr, size_t bytes)
{
    u8* p = (u8*)ptr;
    while (bytes--)
        *p++ = 0;
}

static void vcpu_init_slot(vcpu_t* vcpu, int id, u64 entry, u64 stack, u64 vttbr_snapshot)
{
    memclr(vcpu, sizeof(*vcpu));
    vcpu->arch.cntvoff_el2 = 0;
    asm volatile("mrs %0, TTBR0_EL1" : "=r"(vcpu->arch.tf.ttbr0_el1));
    asm volatile("mrs %0, TTBR1_EL1" : "=r"(vcpu->arch.tf.ttbr1_el1));
    asm volatile("mrs %0, TCR_EL1" : "=r"(vcpu->arch.tf.tcr_el1));
    asm volatile("mrs %0, SCTLR_EL1" : "=r"(vcpu->arch.tf.sctlr_el1));
    asm volatile("mrs %0, TPIDR_EL1" : "=r"(vcpu->arch.tf.tpidr_el1));
    asm volatile("mrs %0, CNTKCTL_EL1" : "=r"(vcpu->arch.tf.cntkctl_el1));
    asm volatile("mrs %0, CNTP_CTL_EL0" : "=r"(vcpu->arch.tf.cntp_ctl_el0));
    asm volatile("mrs %0, CNTP_CVAL_EL0" : "=r"(vcpu->arch.tf.cntp_cval_el0));
    vcpu->arch.tf.cntp_cval_el0 += vcpu->arch.cntvoff_el2;
    asm volatile("mrs %0, CNTV_CTL_EL0" : "=r"(vcpu->arch.tf.cntv_ctl_el0));
    asm volatile("mrs %0, CNTV_CVAL_EL0" : "=r"(vcpu->arch.tf.cntv_cval_el0));
    vcpu->arch.tf.elr_el1 = entry;
    vcpu->arch.tf.sp_el1 = stack;
    vcpu->arch.tf.regs[0] = (u64)id;
    const u64 SPSR_EL1H = 0x5ull | (0xFull << 6);
    vcpu->arch.tf.spsr_el1 = SPSR_EL1H;
    vcpu->arch.vttbr_el2 = vttbr_snapshot;
    u64 cntpct;
    asm volatile("mrs %0, CNTPCT_EL0" : "=r"(cntpct));
    vcpu->arch.cntvct_el0 = cntpct; // start virtual counter aligned with physical
    vcpu->vcpu_id = id;
}

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

    u64 vttbr_snapshot;
    asm volatile("mrs %0, VTTBR_EL2" : "=r"(vttbr_snapshot));

    vcpu_init_slot(&vcpu_pool[0], 0, (u64)guest_counter_os, 0x40080000ull, vttbr_snapshot);
    vcpu_init_slot(&vcpu_pool[1], 1, (u64)guest_memwalk_os, 0x400A0000ull, vttbr_snapshot);

    vcpu_scheduler_register(&vcpu_pool[0]);
    vcpu_scheduler_register(&vcpu_pool[1]);
    vcpu_scheduler_set_current(&vcpu_pool[0]);

    console_puts("EL2: Launching initial VCPU...\n");
    vcpu_run(&vcpu_pool[0]);
}
