#include <stdint.h>
#include "vcpu.h"
#include "guest_api.h"

extern void console_puts(const char*);
extern void console_hex64(uint64_t);

static bool handle_guest_task_report(uint64_t elr)
{
    (void)elr;
    vcpu_t *current = vcpu_scheduler_current();
    if (!current)
        return false;

    uint64_t ptr = current->arch.tf.regs[1];
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
    return true;
}

static bool handle_guest_hvc(uint64_t esr, uint64_t elr)
{
    const uint64_t imm16 = esr & 0xFFFF;
    if (imm16 == 0x60)
        return handle_guest_task_report(elr);
    if (imm16 == 0x63) {
        vcpu_t *current = vcpu_scheduler_current();
        console_puts("EL2: guest synchronous exception report\n");
        if (current) {
            uint64_t guest_esr = current->arch.tf.regs[0];
            uint64_t guest_elr = current->arch.tf.regs[1];
            console_puts("  guest ESR_EL1: "); console_hex64(guest_esr); console_puts("\n");
            console_puts("  guest ELR_EL1: "); console_hex64(guest_elr); console_puts("\n");
        }
        for (;;)
            asm volatile("wfi");
    }
    return false;
}

void el2_exception_common(uint64_t esr, uint64_t elr, uint64_t spsr, uint64_t far, uint64_t code) {
    uint64_t ec = (esr >> 26) & 0x3F; // Exception Class

    if (ec == 0x01) {
        console_puts("EL2: WFI/WFE from guest detected, yielding...\n");
        uint64_t next = elr + 4; // skip WFI/WFE
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
        uint64_t vtcr;
        asm volatile("mrs %0, VTCR_EL2":"=r"(vtcr));
        uint64_t vttbr;
        asm volatile("mrs %0, VTTBR_EL2":"=r"(vttbr));
        uint64_t hpfar;
        asm volatile("mrs %0, HPFAR_EL2":"=r"(hpfar));
        console_puts("VTTBR_EL2: "); console_hex64(vttbr); console_puts("\n");
        console_puts("VTCR_EL2 : "); console_hex64(vtcr); console_puts("\n");
        console_puts("HPFAR_EL2 : "); console_hex64(hpfar); console_puts("\n");

        // Decode ISS for aborts
        uint64_t iss = esr & 0xFFFFFF; // low 24 bits
        uint64_t ifsc = iss & 0x3F;    // bits[5:0] Fault Status Code
        console_puts("ISS: "); console_hex64(iss); console_puts("\n");
        console_puts("  IFSC: "); console_hex64(ifsc); console_puts("\n");
        if (ifsc == 0x4 || ifsc == 0x5 || ifsc == 0x6) {
            console_puts("  LVL: "); console_hex64(ifsc - 0x4); console_puts("\n");
        }
        uint64_t fnv = (iss >> 10) & 1;
        uint64_t ea  = (iss >> 9) & 1;
        uint64_t s1ptw = (iss >> 7) & 1;
        console_puts("  S1PTW: "); console_hex64(s1ptw); console_puts("\n");
        console_puts("  FnV: "); console_hex64(fnv); console_puts("\n");
        console_puts("  EA: "); console_hex64(ea); console_puts("\n");

        // Compute Stage-2 L1 index & dump descriptor at runtime.
        uint64_t ipa_index = (far >> 30) & 0x1FF; // L1 index for 1GB block (TG0=4K, start L1)
        console_puts("S2 L1 idx for FAR: "); console_hex64(ipa_index); console_puts("\n");
        // Extract base address of S2 L1 from VTTBR_EL2 (bits[47:0])
        uint64_t l1_base = vttbr & ((1ull<<48)-1);
        uint64_t* l1 = (uint64_t*)l1_base;
        uint64_t entry = l1[ipa_index];
        console_puts("S2 L1 entry value : "); console_hex64(entry); console_puts("\n");
        if ((entry & 0x1) == 0) {
            console_puts("S2 L1 entry NOT VALID -> translation fault\n");
        } else {
            console_puts("S2 L1 entry valid.\n");
        }

    }
    for(;;) asm volatile("wfi"); // hang
}
