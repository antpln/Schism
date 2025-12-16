#include <stdint.h>
#include "vcpu.h"
#include "guest_api.h"
#include "guest_layout.h"

extern void console_puts(const char*);
extern void console_hex64(u64);

#define SYS_REG_ENCODE(op0, op1, crn, crm, op2) \
    ((u32)(((op0) << 14) | ((op1) << 10) | ((crn) << 6) | ((crm) << 2) | (op2)))


// Timer virtualization strategy:
// - Each vCPU keeps its own virtual counter value (saved in cntvct_el0). On entry we program CNTVOFF_EL2
//   so that CNTVCT reads match that saved value plus elapsed host time since scheduling in.
// - CNTHCTL_EL2 traps physical timer/counter sysregs (CNTPCT/CNTP_*). When EC=0x18 triggers, we translate
//   CNTP_* accesses between the guest's virtual count and the hardware physical counter using CNTVOFF_EL2.
//   CNTV_* accesses are passed through because hardware already applies CNTVOFF_EL2.
// - Guests can optionally rebase their virtual time via HVC #0x61 (handle_guest_time_override), which
//   recomputes CNTVOFF_EL2 and reprograms CNTP/CNTV compares so pending timers stay coherent.
// The encodings below decode ESR_EL2 values for EC=0x18 (trapped MSR/MRS/System instructions) so we know
// which counter/timer sysreg the guest touched.
// - CNTPCT_EL0: physical counter; CNTVOFF is not applied.
// - CNTVCT_EL0: virtual counter with CNTVOFF applied.
// - CNTP_*: physical timer compare/control; we translate to/from virtual counts via CNTVOFF.
// - CNTV_*: virtual timer compare/control; already in the virtual domain.
enum {
    SYS_CNTPCT_EL0    = SYS_REG_ENCODE(3, 3, 14, 0, 1), // Physical Count Register
    SYS_CNTVCT_EL0    = SYS_REG_ENCODE(3, 3, 14, 0, 2), // Virtual Count Register
    SYS_CNTP_TVAL_EL0 = SYS_REG_ENCODE(3, 3, 14, 2, 0), // Physical Timer TimerValue Register
    SYS_CNTP_CTL_EL0  = SYS_REG_ENCODE(3, 3, 14, 2, 1), // Physical Timer Control Register
    SYS_CNTP_CVAL_EL0 = SYS_REG_ENCODE(3, 3, 14, 2, 2), // Physical Timer CompareValue Register
    SYS_CNTV_TVAL_EL0 = SYS_REG_ENCODE(3, 3, 14, 3, 0), // Virtual Timer TimerValue Register
    SYS_CNTV_CTL_EL0  = SYS_REG_ENCODE(3, 3, 14, 3, 1), // Virtual Timer Control Register
    SYS_CNTV_CVAL_EL0 = SYS_REG_ENCODE(3, 3, 14, 3, 2), // Virtual Timer CompareValue Register
};

// Decode the trapped system register from an ESR_EL2 value for EC=0x18 (sysreg trap).
static inline u32 esr_sys64_sysreg(u64 esr)
{
    u64 iss = esr & 0x1ffffffu;
    u32 op0 = (u32)((iss >> 20) & 0x3u);
    u32 op1 = (u32)((iss >> 16) & 0xfu);
    u32 crn = (u32)((iss >> 12) & 0xfu);
    u32 crm = (u32)((iss >> 8) & 0xfu);
    u32 op2 = (u32)((iss >> 5) & 0x7u);
    return SYS_REG_ENCODE(op0, op1, crn, crm, op2);
}

// Extract the RT field (bits[4:0]) of ISS for EC=0x18 sysreg access.
static inline u32 esr_sys64_rt(u64 esr)
{
    return (u32)(esr & 0x1fu);
}

// DIR bit of ISS tells whether the trapped sysreg access was a read (1) or write (0).
static inline bool esr_sys64_is_read(u64 esr)
{
    return ((esr >> 21) & 0x1u) != 0;
}

// Read the current virtual counter (CNTVCT_EL0) with CNTVOFF already applied.
static inline u64 virtual_counter_now(void)
{
    u64 val;
    asm volatile("mrs %0, CNTVCT_EL0" : "=r"(val));
    return val;
}

// Move ELR_EL2 (and the cached trapframe ELR_EL1) past the trapped instruction.
static void advance_guest_elr(vcpu_t *current, u64 elr)
{
    u64 next = elr + 4;
    asm volatile("msr ELR_EL2, %0" :: "r"(next));
    if (current)
        current->arch.tf.elr_el1 = next;
}

// Print a guest-supplied task report (HVC #0x60) and return whether it was handled.
static bool handle_guest_task_report(u64 elr)
{
    (void)elr;
    vcpu_t *current = vcpu_scheduler_current();
    if (!current)
        return false;

    u64 ptr = current->arch.tf.regs[1];
    const struct guest_task_result *res = (const struct guest_task_result *)ptr;
    if (!res)
        return true;

    console_puts("[guest");
    char suffix[3] = { '0' + (char)current->vcpu_id, ']', '\0' };
    console_puts(suffix);
    console_puts(" ");
    console_puts(res->desc);
    console_puts(" data0=");
    console_hex64(res->data0);
    console_puts(" data1=");
    console_hex64(res->data1);
    console_puts("\n");

    // Report timer telemetry carried in the guest task result to validate virtual time isolation.
    if (res->time_before || res->time_after || res->time_target || res->memwalk_time)
    {
        console_puts("  timers: before=");
        console_hex64(res->time_before);
        console_puts(" after=");
        console_hex64(res->time_after);
        console_puts(" target=");
        console_hex64(res->time_target);
        console_puts(" memwalk_time=");
        console_hex64(res->memwalk_time);
        console_puts("\n");
    }
    return true;
}

// Handle trapped accesses to CNT* timer sysregs (EC=0x18) and emulate them with virtual time.
// Physical timer registers (CNTP_*) are converted between virtual and physical counter domains via CNTVOFF.
// Virtual timer registers (CNTV_*) are passed through because CNTVOFF_EL2 already biases CNTVCT.
static bool handle_timer_sysreg(u64 esr, u64 elr)
{
    vcpu_t *current = vcpu_scheduler_current();
    if (!current)
        return false;

    const u32 sysreg = esr_sys64_sysreg(esr);
    const u32 rt = esr_sys64_rt(esr);
    const bool is_read = esr_sys64_is_read(esr);

    u64 virt_now = virtual_counter_now();

    switch (sysreg)
    {
        case SYS_CNTPCT_EL0:
        case SYS_CNTVCT_EL0:
            // Read the virtualized counter (returns CNTVCT with CNTVOFF applied). Writes never occur.
            if (is_read && rt < 31)
                current->arch.tf.regs[rt] = virt_now;
            advance_guest_elr(current, elr);
            return true;

        case SYS_CNTP_CVAL_EL0:
        {
            if (is_read)
            {
                // Read CNTP_CVAL: fetch physical compare value, add CNTVOFF to present a virtual count.
                u64 phys;
                asm volatile("mrs %0, CNTP_CVAL_EL0" : "=r"(phys));
                u64 virt_val = phys + current->arch.cntvoff_el2;
                current->arch.tf.cntp_cval_el0 = virt_val;
                if (rt < 31)
                    current->arch.tf.regs[rt] = virt_val;
            }
            else
            {
                // Write CNTP_CVAL: guest supplies a virtual count; convert back to physical and program CNTP_CVAL_EL0.
                u64 virt_val = (rt < 31) ? current->arch.tf.regs[rt] : 0;
                current->arch.tf.cntp_cval_el0 = virt_val;
                u64 phys_val = virt_val - current->arch.cntvoff_el2;
                asm volatile("msr CNTP_CVAL_EL0, %0" : : "r"(phys_val));
            }
            advance_guest_elr(current, elr);
            asm volatile("isb");
            return true;
        }

        case SYS_CNTP_CTL_EL0:
        {
            if (is_read)
            {
                // Read CNTP_CTL: expose the hardware control bits (EN/IMASK/ISTATUS) to the guest.
                u64 ctl;
                asm volatile("mrs %0, CNTP_CTL_EL0" : "=r"(ctl));
                current->arch.tf.cntp_ctl_el0 = ctl;
                if (rt < 31)
                    current->arch.tf.regs[rt] = ctl;
            }
            else
            {
                // Write CNTP_CTL: accept guest EN/IMASK and program hardware; ISTATUS is read-only.
                u64 ctl = (rt < 31) ? current->arch.tf.regs[rt] : 0;
                ctl &= 0x3u; // only enable and IMASK are writable
                current->arch.tf.cntp_ctl_el0 = ctl;
                asm volatile("msr CNTP_CTL_EL0, %0" : : "r"(ctl));
            }
            advance_guest_elr(current, elr);
            asm volatile("isb");
            return true;
        }

        case SYS_CNTP_TVAL_EL0:
        {
            if (is_read)
            {
                // Read CNTP_TVAL: return (virtual CVAL - virtual counter) as a signed 32-bit delta.
                u64 virt_cval = current->arch.tf.cntp_cval_el0;
                s64 delta = (s64)(virt_cval - virt_now);
                if (rt < 31)
                    current->arch.tf.regs[rt] = (u64)delta;
            }
            else
            {
                // Write CNTP_TVAL: treat guest value as signed delta, derive absolute virtual target, then program CNTP_CVAL.
                u64 raw = (rt < 31) ? current->arch.tf.regs[rt] : 0;
                s64 delta = (int32_t)raw; // TVAL is a signed 32-bit offset
                u64 target = virt_now + delta;
                current->arch.tf.cntp_cval_el0 = target;
                u64 phys_target = target - current->arch.cntvoff_el2;
                asm volatile("msr CNTP_CVAL_EL0, %0" : : "r"(phys_target));
            }
            advance_guest_elr(current, elr);
            asm volatile("isb");
            return true;
        }

        case SYS_CNTV_CVAL_EL0:
        {
            if (is_read)
            {
                // Read CNTV_CVAL: pass through the virtual timer compare value as-is.
                u64 val;
                asm volatile("mrs %0, CNTV_CVAL_EL0" : "=r"(val));
                current->arch.tf.cntv_cval_el0 = val;
                if (rt < 31)
                    current->arch.tf.regs[rt] = val;
            }
            else
            {
                // Write CNTV_CVAL: program the virtual timer compare value directly.
                u64 val = (rt < 31) ? current->arch.tf.regs[rt] : 0;
                current->arch.tf.cntv_cval_el0 = val;
                asm volatile("msr CNTV_CVAL_EL0, %0" : : "r"(val));
            }
            advance_guest_elr(current, elr);
            asm volatile("isb");
            return true;
        }

        case SYS_CNTV_CTL_EL0:
        {
            if (is_read)
            {
                // Read CNTV_CTL: expose virtual timer control bits (EN/IMASK/ISTATUS).
                u64 ctl;
                asm volatile("mrs %0, CNTV_CTL_EL0" : "=r"(ctl));
                current->arch.tf.cntv_ctl_el0 = ctl;
                if (rt < 31)
                    current->arch.tf.regs[rt] = ctl;
            }
            else
            {
                // Write CNTV_CTL: accept guest EN/IMASK and program virtual timer control.
                u64 ctl = (rt < 31) ? current->arch.tf.regs[rt] : 0;
                ctl &= 0x3u;
                current->arch.tf.cntv_ctl_el0 = ctl;
                asm volatile("msr CNTV_CTL_EL0, %0" : : "r"(ctl));
            }
            advance_guest_elr(current, elr);
            asm volatile("isb");
            return true;
        }

        case SYS_CNTV_TVAL_EL0:
        {
            if (is_read)
            {
                // Read CNTV_TVAL: return (CNTV_CVAL - CNTVCT) as signed delta.
                u64 val;
                asm volatile("mrs %0, CNTV_CVAL_EL0" : "=r"(val));
                current->arch.tf.cntv_cval_el0 = val;
                s64 delta = (s64)(val - virt_now);
                if (rt < 31)
                    current->arch.tf.regs[rt] = (u64)delta;
            }
            else
            {
                // Write CNTV_TVAL: treat guest value as signed delta and program CNTV_CVAL accordingly.
                u64 raw = (rt < 31) ? current->arch.tf.regs[rt] : 0;
                s64 delta = (int32_t)raw;
                u64 target = virt_now + delta;
                current->arch.tf.cntv_cval_el0 = target;
                asm volatile("msr CNTV_CVAL_EL0, %0" : : "r"(target));
            }
            advance_guest_elr(current, elr);
            asm volatile("isb");
            return true;
        }

        default:
            break;
    }

    return false;
}

// Adjust CNTVOFF_EL2 and timer hardware when a guest asks to set its virtual time (HVC #0x61).
static bool handle_guest_time_override(void)
{
    vcpu_t *current = vcpu_scheduler_current();
    if (!current)
        return false;

    u64 desired = current->arch.tf.regs[0]; // x0 holds target virtual counter value
    u64 phys_counter;
    asm volatile("mrs %0, CNTPCT_EL0" : "=r"(phys_counter));

    u64 offset = desired - phys_counter;
    current->arch.cntvct_el0 = desired;
    current->arch.cntvoff_el2 = offset;
    asm volatile("msr CNTVOFF_EL2, %0" : : "r"(offset) : "memory");
    u64 phys_cval = current->arch.tf.cntp_cval_el0 - offset;
    asm volatile("msr CNTP_CVAL_EL0, %0" : : "r"(phys_cval));
    asm volatile("msr CNTP_CTL_EL0, %0" : : "r"(current->arch.tf.cntp_ctl_el0));
    asm volatile("msr CNTV_CVAL_EL0, %0" : : "r"(current->arch.tf.cntv_cval_el0));
    asm volatile("msr CNTV_CTL_EL0, %0" : : "r"(current->arch.tf.cntv_ctl_el0));
    asm volatile("isb");
    current->arch.tf.regs[0] = desired; // return the applied value in x0
    return true;
}

// Dispatch hypercalls issued as HVC (currently report/time override/debug traps).
static bool handle_guest_hvc(u64 esr, u64 elr)
{
    const u64 imm16 = esr & 0xFFFF;
    if (imm16 == 0x60)
        return handle_guest_task_report(elr);
    if (imm16 == 0x61)
        return handle_guest_time_override();
    if (imm16 == 0x63) {
        vcpu_t *current = vcpu_scheduler_current();
        console_puts("EL2: guest synchronous exception report\n");
        if (current) {
            u64 guest_esr = current->arch.tf.regs[0];
            u64 guest_elr = current->arch.tf.regs[1];
            console_puts("  guest ESR_EL1: "); console_hex64(guest_esr); console_puts("\n");
            console_puts("  guest ELR_EL1: "); console_hex64(guest_elr); console_puts("\n");
        }
        for (;;)
            asm volatile("wfi");
    }
    return false;
}

// Top-level EL2 exception handler: decode EC, fast-path known traps, and dump state otherwise.
void el2_exception_common(u64 esr, u64 elr, u64 spsr, u64 far, u64 code) {
    u64 ec = (esr >> 26) & 0x3F; // Exception Class

    if (ec == 0x01) {
        console_puts("EL2: WFI/WFE from guest detected, yielding...\n");
        u64 next = elr + 4; // skip WFI/WFE
        asm volatile("msr ELR_EL2, %0" :: "r"(next)); // Advance ELR_EL2

        vcpu_t* current = vcpu_scheduler_current();
        if (current) {
            current->arch.tf.elr_el1 = next;
            current->request_yield = true;
        }

        return;
    }

    if (ec == 0x16 && handle_guest_hvc(esr, elr))
        return;
    if (ec == 0x18 && handle_timer_sysreg(esr, elr))
        return;

    console_puts("\n=== EL2 Exception ===\n");
    console_puts("ESR: "); console_hex64(esr); console_puts("\n");
    console_puts("ELR: "); console_hex64(elr); console_puts("\n");
    console_puts("SPSR: "); console_hex64(spsr); console_puts("\n");
    console_puts("FAR: "); console_hex64(far); console_puts("\n");
    console_puts("Code: "); console_hex64(code); console_puts("\n");
    console_puts("====================\n");

    console_puts("Exception Class (EC): "); console_hex64(ec); console_puts("\n");

    // AArch64 ESR_ELx EC:
    // 0x20: Instruction Abort from lower EL
    // 0x21: Instruction Abort from same EL
    // 0x24: Data Abort from lower EL
    // 0x25: Data Abort from same EL
    if (ec == 0x20 || ec == 0x21 || ec == 0x24 || ec == 0x25) {
        switch (ec) {
            case 0x20: console_puts("Instruction Abort from lower EL detected.\n"); break;
            case 0x21: console_puts("Instruction Abort from same EL detected.\n"); break;
            case 0x24: console_puts("Data Abort from lower EL detected.\n"); break;
            case 0x25: console_puts("Data Abort from same EL detected.\n"); break;
        }
        u64 vtcr;
        asm volatile("mrs %0, VTCR_EL2":"=r"(vtcr));
        u64 vttbr;
        asm volatile("mrs %0, VTTBR_EL2":"=r"(vttbr));
        u64 hpfar;
        asm volatile("mrs %0, HPFAR_EL2":"=r"(hpfar));
        console_puts("VTTBR_EL2: "); console_hex64(vttbr); console_puts("\n");
        console_puts("VTCR_EL2 : "); console_hex64(vtcr); console_puts("\n");
        console_puts("HPFAR_EL2 : "); console_hex64(hpfar); console_puts("\n");

        // Decode ISS for aborts
        u64 iss = esr & 0xFFFFFF; // low 24 bits
        u64 ifsc = iss & 0x3F;    // bits[5:0] Fault Status Code
        console_puts("ISS: "); console_hex64(iss); console_puts("\n");
        console_puts("  IFSC: "); console_hex64(ifsc); console_puts("\n");
        if (ifsc == 0x4 || ifsc == 0x5 || ifsc == 0x6) {
            console_puts("  LVL: "); console_hex64(ifsc - 0x4); console_puts("\n");
        }
        u64 fnv = (iss >> 10) & 1;
        u64 ea  = (iss >> 9) & 1;
        u64 s1ptw = (iss >> 7) & 1;
        console_puts("  S1PTW: "); console_hex64(s1ptw); console_puts("\n");
        console_puts("  FnV: "); console_hex64(fnv); console_puts("\n");
        console_puts("  EA: "); console_hex64(ea); console_puts("\n");

        // Compute Stage-2 L1 index & dump descriptor at runtime.
        u64 ipa_index = (far >> 30) & 0x1FF; // L1 index for 1GB block (TG0=4K, start L1)
        console_puts("S2 L1 idx for FAR: "); console_hex64(ipa_index); console_puts("\n");
        // Extract base address of S2 L1 from VTTBR_EL2 (bits[47:0])
        u64 l1_base = vttbr & ((1ull<<48)-1);
        u64* l1 = (u64*)l1_base;
        u64 entry = l1[ipa_index];
        console_puts("S2 L1 entry value : "); console_hex64(entry); console_puts("\n");
        if ((entry & 0x1) == 0) {
            console_puts("S2 L1 entry NOT VALID -> translation fault\n");
        } else {
            console_puts("S2 L1 entry valid.\n");
        }

    }
    for(;;) asm volatile("wfi"); // hang
}
