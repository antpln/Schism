// This stub file triggers a Stage-2 fault.

.section .text, "ax" // executable code section

.global el1_start
el1_start:
    mov x0, #0x40000000      // Load an address in the Stage-2 mapped region
    str x0, [x0] // This shouldn't trigger a Stage-2 fault
1:  wfi // idle until interrupt
    b     1b
