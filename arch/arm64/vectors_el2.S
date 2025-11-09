.section .text, "ax" // executable code section
.align 11 // align to 2KB boundary as required for vector tables
.global el2_vectors
.extern current_trapframe
el2_vectors:

.macro SLOT label, code // define a vector slot
  .align 7
  .global \label
\label: 
    str x16, [sp, #-16]!                // preserve x16 before using as scratch
    adrp x16, current_trapframe
    ldr x16, [x16, :lo12:current_trapframe]
    cbz x16, 2f                         // skip save if no trapframe requested

    // At this point x16 holds the pointer to the trapframe that should capture
    // the outgoing guest state. All guest GPRs are still live in x0-x30, with
    // the original x16 stored on the stack. We emit paired stores directly to
    // the trapframe so the expensive path runs only when a world-switch is in
    // flight.
    stp x0, x1, [x16, #(8 * 0)]
    stp x2, x3, [x16, #(8 * 2)]
    stp x4, x5, [x16, #(8 * 4)]
    stp x6, x7, [x16, #(8 * 6)]
    stp x8, x9, [x16, #(8 * 8)]
    stp x10, x11, [x16, #(8 * 10)]
    stp x12, x13, [x16, #(8 * 12)]
    stp x14, x15, [x16, #(8 * 14)]

    ldr x0, [sp]                         // recover original x16 value
    stp x0, x17, [x16, #(8 * 16)]
    stp x18, x19, [x16, #(8 * 18)]
    stp x20, x21, [x16, #(8 * 20)]
    stp x22, x23, [x16, #(8 * 22)]
    stp x24, x25, [x16, #(8 * 24)]
    stp x26, x27, [x16, #(8 * 26)]
    stp x28, x29, [x16, #(8 * 28)]
    str x30, [x16, #(8 * 30)]

    mrs x0, SP_EL1
    str x0, [x16, #(8 * 31)]
    mrs x0, ELR_EL2
    str x0, [x16, #(8 * 32)]
    mrs x0, SPSR_EL2
    str x0, [x16, #(8 * 33)]

    // Clear the latch so subsequent traps skip the heavy save path
    adrp x1, current_trapframe
    str xzr, [x1, :lo12:current_trapframe]

2:  ldr x16, [sp]
    add sp, sp, #16

    mrs x0, esr_el2 // read exception syndrome register
    mrs x1, elr_el2 // read exception link register
    mrs x2, spsr_el2 // read saved program status register
    mrs x3, far_el2 // read fault address register
    mov x4, #\code // pass code identifying the exception type
    bl el2_exception_common
    eret
.endm

SLOT el2_sync_sp0,   0x00
SLOT el2_irq_sp0,    0x01
SLOT el2_fiq_sp0,    0x02
SLOT el2_serr_sp0,   0x03
SLOT el2_sync_spx,   0x10
SLOT el2_irq_spx,    0x11
SLOT el2_fiq_spx,    0x12
SLOT el2_serr_spx,   0x13
SLOT el2_sync_a64,   0x20
SLOT el2_irq_a64,    0x21
SLOT el2_fiq_a64,    0x22
SLOT el2_serr_a64,   0x23
SLOT el2_sync_a32,   0x30
SLOT el2_irq_a32,    0x31
SLOT el2_fiq_a32,    0x32
SLOT el2_serr_a32,   0x33
