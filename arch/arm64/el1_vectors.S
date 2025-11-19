.section .text, "ax"
.align 11
.global guest_el1_vectors

guest_el1_vectors:
.macro GUEST_SLOT label, handler=.
  .align 7
\label:
    b \handler
.endm

// Route unexpected synchronous exceptions to EL2 for diagnosis via HVC #0x63.
// Other vector slots still spin because the guests do not program them yet.
GUEST_SLOT guest_el1_sync_sp0, guest_el1_sync_common
GUEST_SLOT guest_el1_irq_sp0
GUEST_SLOT guest_el1_fiq_sp0
GUEST_SLOT guest_el1_serr_sp0
GUEST_SLOT guest_el1_sync_spx, guest_el1_sync_common
GUEST_SLOT guest_el1_irq_spx
GUEST_SLOT guest_el1_fiq_spx
GUEST_SLOT guest_el1_serr_spx
GUEST_SLOT guest_el1_sync_a64
GUEST_SLOT guest_el1_irq_a64
GUEST_SLOT guest_el1_fiq_a64
GUEST_SLOT guest_el1_serr_a64
GUEST_SLOT guest_el1_sync_a32
GUEST_SLOT guest_el1_irq_a32
GUEST_SLOT guest_el1_fiq_a32
GUEST_SLOT guest_el1_serr_a32

guest_el1_sync_common:
    mrs x0, esr_el1
    mrs x1, elr_el1
    hvc #0x63
1:  b 1b
