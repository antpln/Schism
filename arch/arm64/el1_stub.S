// Minimal EL1 payload that stores into the Stage-2 window to sanity-check mappings.

.section .text, "ax" // executable code section

.global el1_start
el1_start:
    mov x0, #0x40000000      // Load an address in the Stage-2 mapped region
    str x0, [x0]             // Store into the identity-mapped region (should succeed)
1:  wfi // idle until interrupt
    b     1b
