.section .text, "ax"
.align 11
.global guest_el1_vectors

guest_el1_vectors:
.macro GUEST_SLOT label
  .align 7
\label:
    b \label
.endm

GUEST_SLOT guest_el1_sync_sp0
GUEST_SLOT guest_el1_irq_sp0
GUEST_SLOT guest_el1_fiq_sp0
GUEST_SLOT guest_el1_serr_sp0
GUEST_SLOT guest_el1_sync_spx
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
