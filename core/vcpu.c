#include <stdint.h>
#include <stdbool.h>
#include "s2_mmu.h"
#include "vcpu.h"
#include <stddef.h>

// Forward declarations to avoid missing uart_pl011.h dependency.
void console_puts(const char *s);
void console_hex64(uint64_t v);

extern void vcpu_switch_asm(trapframe_t *tf);
void world_switch(vcpu_t *from, vcpu_t *to);

trapframe_t *current_trapframe = NULL;

#define VCPU_SCHED_MAX 8
static vcpu_t* sched_runqueue[VCPU_SCHED_MAX];
static size_t sched_len;
static size_t sched_idx;
static vcpu_t* sched_current;

static int sched_find_slot(vcpu_t* vcpu)
{
    for (size_t i = 0; i < sched_len; ++i)
        if (sched_runqueue[i] == vcpu)
            return (int)i;
    return -1;
}

void vcpu_scheduler_register(vcpu_t* vcpu)
{
    if (!vcpu || sched_len >= VCPU_SCHED_MAX)
        return;
    if (sched_find_slot(vcpu) >= 0)
        return;
    sched_runqueue[sched_len++] = vcpu;
    if (!sched_current)
    {
        sched_current = vcpu;
        sched_idx = sched_len - 1;
    }
}

void vcpu_scheduler_set_current(vcpu_t* vcpu)
{
    if (!vcpu)
        return;
    int slot = sched_find_slot(vcpu);
    if (slot < 0 && sched_len < VCPU_SCHED_MAX)
    {
        sched_runqueue[sched_len] = vcpu;
        slot = (int)sched_len;
        sched_len++;
    }
    if (slot >= 0)
    {
        sched_current = vcpu;
        sched_idx = (size_t)slot;
    }
}

vcpu_t* vcpu_scheduler_current(void)
{
    return sched_current;
}

bool vcpu_scheduler_yield(void)
{
    if (sched_len <= 1 || !sched_current)
        return false;

    size_t next = (sched_idx + 1) % sched_len;
    vcpu_t* target = sched_runqueue[next];
    if (!target || target == sched_current)
        return false;

    vcpu_t* prev = sched_current;
    sched_current = target;
    sched_idx = next;

    world_switch(prev, target);
    return true;
}

void vcpu_run(vcpu_t* vcpu)
{
    if (!vcpu)
        return;
    vcpu_scheduler_set_current(vcpu);
    world_switch(NULL, vcpu);
}

static void save_fp(vcpu_t *vcpu)
{
    if (!vcpu)
        return;

    uint8_t *base = (uint8_t *)vcpu->arch.fp.vregs;
    // Store Q0-Q31 registers
    asm volatile(
        "stp q0, q1, [%0]\n" // This stores Q0 and Q1 (each 128 bits) at offset 0 of the fp vregs array
        "stp q2, q3, [%0, #32]\n"
        "stp q4, q5, [%0, #64]\n"
        "stp q6, q7, [%0, #96]\n"
        "stp q8, q9, [%0, #128]\n"
        "stp q10, q11, [%0, #160]\n"
        "stp q12, q13, [%0, #192]\n"
        "stp q14, q15, [%0, #224]\n"
        "stp q16, q17, [%0, #256]\n"
        "stp q18, q19, [%0, #288]\n"
        "stp q20, q21, [%0, #320]\n"
        "stp q22, q23, [%0, #352]\n"
        "stp q24, q25, [%0, #384]\n"
        "stp q26, q27, [%0, #416]\n"
        "stp q28, q29, [%0, #448]\n"
        "stp q30, q31, [%0, #480]\n"
        :
        : "r"(base)
        : "memory");

    uint64_t tmp;
    asm volatile("mrs %0, FPCR" : "=r"(tmp)); // Read FPCR (Floating Point Control Register)
    vcpu->arch.fp.fpcr = (u32)tmp;
    asm volatile("mrs %0, FPSR" : "=r"(tmp)); // Read FPSR (Floating Point Status Register)
    vcpu->arch.fp.fpsr = (u32)tmp;
    vcpu->arch.fp.used = 1;
}

static void restore_fp(vcpu_t *vcpu)
{
    if (!vcpu || !vcpu->arch.fp.used)
        return;

    const uint8_t *base = (const uint8_t *)vcpu->arch.fp.vregs;

    asm volatile(
        "ldp q0, q1, [%0]\n"
        "ldp q2, q3, [%0, #32]\n"
        "ldp q4, q5, [%0, #64]\n"
        "ldp q6, q7, [%0, #96]\n"
        "ldp q8, q9, [%0, #128]\n"
        "ldp q10, q11, [%0, #160]\n"
        "ldp q12, q13, [%0, #192]\n"
        "ldp q14, q15, [%0, #224]\n"
        "ldp q16, q17, [%0, #256]\n"
        "ldp q18, q19, [%0, #288]\n"
        "ldp q20, q21, [%0, #320]\n"
        "ldp q22, q23, [%0, #352]\n"
        "ldp q24, q25, [%0, #384]\n"
        "ldp q26, q27, [%0, #416]\n"
        "ldp q28, q29, [%0, #448]\n"
        "ldp q30, q31, [%0, #480]\n"
        :
        : "r"(base)
        : "memory");

    uint64_t tmp = vcpu->arch.fp.fpcr;
    asm volatile("msr FPCR, %0" : : "r"(tmp));
    tmp = vcpu->arch.fp.fpsr;
    asm volatile("msr FPSR, %0" : : "r"(tmp));
}

static void save_sve(vcpu_t *vcpu)
{
    if (!vcpu)
        return;

    vcpu->arch.sve.used = 0; // SVE save not yet implemented.
}

static void restore_sve(vcpu_t *vcpu)
{
    (void)vcpu; // Nothing to restore until SVE support is added.
}

static void save_pauth(vcpu_t *vcpu)
{
    if (!vcpu)
        return;

#ifdef __ARM_FEATURE_PAUTH
    asm volatile("mrs %0, APIAKEY_EL1" : "=r"(vcpu->arch.pauth.apia));
    asm volatile("mrs %0, APIBKEY_EL1" : "=r"(vcpu->arch.pauth.apib));
    asm volatile("mrs %0, APDAKEY_EL1" : "=r"(vcpu->arch.pauth.apda));
    asm volatile("mrs %0, APDBKEY_EL1" : "=r"(vcpu->arch.pauth.apdb));

    asm volatile("msr APIAKEY_EL1, xzr"); // Clear keys from registers after saving
    asm volatile("msr APIBKEY_EL1, xzr");
    asm volatile("msr APDAKEY_EL1, xzr");
    asm volatile("msr APDBKEY_EL1, xzr");
    asm volatile("isb");
    vcpu->arch.pauth.used = 1;
#else
    vcpu->arch.pauth.used = 0;
#endif
}

static void restore_pauth(vcpu_t *vcpu)
{
    if (!vcpu || !vcpu->arch.pauth.used)
        return;

#ifdef __ARM_FEATURE_PAUTH
    asm volatile("msr APIAKEY_EL1, %0" : : "r"(vcpu->arch.pauth.apia));
    asm volatile("msr APIBKEY_EL1, %0" : : "r"(vcpu->arch.pauth.apib));
    asm volatile("msr APDAKEY_EL1, %0" : : "r"(vcpu->arch.pauth.apda));
    asm volatile("msr APDBKEY_EL1, %0" : : "r"(vcpu->arch.pauth.apdb));
#else
    (void)vcpu;
#endif
}

#define ICH_LR_SYSREG(n) ICH_LR_SYSREG_##n
#define ICH_LR_SYSREG_0  "S3_4_C12_C12_0"
#define ICH_LR_SYSREG_1  "S3_4_C12_C12_1"
#define ICH_LR_SYSREG_2  "S3_4_C12_C12_2"
#define ICH_LR_SYSREG_3  "S3_4_C12_C12_3"
#define ICH_LR_SYSREG_4  "S3_4_C12_C12_4"
#define ICH_LR_SYSREG_5  "S3_4_C12_C12_5"
#define ICH_LR_SYSREG_6  "S3_4_C12_C12_6"
#define ICH_LR_SYSREG_7  "S3_4_C12_C12_7"
#define ICH_LR_SYSREG_8  "S3_4_C12_C13_0"
#define ICH_LR_SYSREG_9  "S3_4_C12_C13_1"
#define ICH_LR_SYSREG_10 "S3_4_C12_C13_2"
#define ICH_LR_SYSREG_11 "S3_4_C12_C13_3"
#define ICH_LR_SYSREG_12 "S3_4_C12_C13_4"
#define ICH_LR_SYSREG_13 "S3_4_C12_C13_5"
#define ICH_LR_SYSREG_14 "S3_4_C12_C13_6"
#define ICH_LR_SYSREG_15 "S3_4_C12_C13_7"
#define ICH_VTR_SYSREG   "S3_4_C12_C11_1"
#define ICH_VMCR_SYSREG    "S3_4_C12_C11_7"
#define ICH_AP0R0_SYSREG   "S3_4_C12_C8_0"

#define VGIC_LR_CAPACITY \
    (sizeof(((vcpu_arch_t *)0)->vgic.lrs) / sizeof(((vcpu_arch_t *)0)->vgic.lrs[0]))

#define SAVE_VGIC_LR_N(n) \
    asm volatile("mrs %0, " ICH_LR_SYSREG(n) : "=r"(vcpu->arch.vgic.lrs[n]))

#define RESTORE_VGIC_LR_N(n) \
    asm volatile("msr " ICH_LR_SYSREG(n) ", %0" : : "r"(vcpu->arch.vgic.lrs[n]))

#define SAVE_VGIC_LR_IF(n, count) \
    do { if ((count) > (n)) { SAVE_VGIC_LR_N(n); } } while (0)

#define RESTORE_VGIC_LR_IF(n, count) \
    do { if ((count) > (n)) { RESTORE_VGIC_LR_N(n); } } while (0)

static size_t vgic_detect_lr_count(void)
{
    uint64_t vtr;
    asm volatile("mrs %0, " ICH_VTR_SYSREG : "=r"(vtr));
    size_t count = (size_t)((vtr & 0xfu) + 1u);
    if (count > VGIC_LR_CAPACITY)
        count = VGIC_LR_CAPACITY;
    return count;
}

static size_t vgic_lr_count(void)
{
    static size_t cached;
    if (!cached)
    {
        cached = vgic_detect_lr_count();
        if (!cached)
            cached = 1; // hardware must implement >=1, but guard anyway
    }
    return cached;
}

static void save_vgic(vcpu_t *vcpu)
{
    if (!vcpu)
        return;

    size_t lr_count = vgic_lr_count();

    SAVE_VGIC_LR_IF(0, lr_count);
    SAVE_VGIC_LR_IF(1, lr_count);
    SAVE_VGIC_LR_IF(2, lr_count);
    SAVE_VGIC_LR_IF(3, lr_count);
    SAVE_VGIC_LR_IF(4, lr_count);
    SAVE_VGIC_LR_IF(5, lr_count);
    SAVE_VGIC_LR_IF(6, lr_count);
    SAVE_VGIC_LR_IF(7, lr_count);
    SAVE_VGIC_LR_IF(8, lr_count);
    SAVE_VGIC_LR_IF(9, lr_count);
    SAVE_VGIC_LR_IF(10, lr_count);
    SAVE_VGIC_LR_IF(11, lr_count);
    SAVE_VGIC_LR_IF(12, lr_count);
    SAVE_VGIC_LR_IF(13, lr_count);
    SAVE_VGIC_LR_IF(14, lr_count);
    SAVE_VGIC_LR_IF(15, lr_count);

    uint64_t tmp;
    asm volatile("mrs %0, " ICH_VMCR_SYSREG : "=r"(tmp)); // Read VMCR (Virtualization Miscellaneous Control Register)
    vcpu->arch.vgic.vmcr = (uint32_t)tmp;
    asm volatile("mrs %0, " ICH_AP0R0_SYSREG : "=r"(tmp)); // Read APR (Active Priority Register 0)
    vcpu->arch.vgic.apr = (uint32_t)tmp;
}

static void restore_vgic(vcpu_t *vcpu)
{
    if (!vcpu)
        return;

    size_t lr_count = vgic_lr_count();

    RESTORE_VGIC_LR_IF(0, lr_count);
    RESTORE_VGIC_LR_IF(1, lr_count);
    RESTORE_VGIC_LR_IF(2, lr_count);
    RESTORE_VGIC_LR_IF(3, lr_count);
    RESTORE_VGIC_LR_IF(4, lr_count);
    RESTORE_VGIC_LR_IF(5, lr_count);
    RESTORE_VGIC_LR_IF(6, lr_count);
    RESTORE_VGIC_LR_IF(7, lr_count);
    RESTORE_VGIC_LR_IF(8, lr_count);
    RESTORE_VGIC_LR_IF(9, lr_count);
    RESTORE_VGIC_LR_IF(10, lr_count);
    RESTORE_VGIC_LR_IF(11, lr_count);
    RESTORE_VGIC_LR_IF(12, lr_count);
    RESTORE_VGIC_LR_IF(13, lr_count);
    RESTORE_VGIC_LR_IF(14, lr_count);
    RESTORE_VGIC_LR_IF(15, lr_count);

    asm volatile("msr " ICH_VMCR_SYSREG ", %0" : : "r"((uint64_t)vcpu->arch.vgic.vmcr)); // Restore VMCR
    asm volatile("msr " ICH_AP0R0_SYSREG ", %0" : : "r"((uint64_t)vcpu->arch.vgic.apr)); // Restore APR
    asm volatile("isb");
}

void world_switch(vcpu_t *from, vcpu_t *to)
{
    // Disable interrupts to harden the switch
    asm volatile("msr daifset, #2"); // Mask IRQs while switching
    asm volatile("isb");
    
    if (from) {
        save_fp(from);
        save_sve(from);
        save_pauth(from);
        save_vgic(from);
    }

    // Switch Stage-2 translation context
    // This register holds the base address of the Stage-2 translation tables
    asm volatile("msr VTTBR_EL2, %0" : : "r"(to->arch.vttbr_el2) : "memory");
    isb(); // ensure new VMID/TTBR selection takes effect

    // Update CNTVOFF_EL2 for the target VCPU
    // This register holds the offset to be applied to the virtual timer
    asm volatile("msr CNTVOFF_EL2, %0" : : "r"(to->arch.cntvoff_el2) : "memory");

    restore_vgic(to);
    restore_pauth(to);
    restore_sve(to);
    restore_fp(to);

    asm volatile("msr VBAR_EL1, %0" :: "r"(guest_el1_vectors) : "memory");

    current_trapframe = &to->arch.tf; // mark target frame for capture on next exit
    console_puts("Switching to VCPU ");
    console_hex64(to->vcpu_id);
    console_puts("\n");

    vcpu_switch_asm(&to->arch.tf); // restores EL1 regs + GPRs and eret
    // Re-enable interrupts after switch
    asm volatile("msr daifclr, #2"); // Re-enable IRQs
    asm volatile("isb");

}
