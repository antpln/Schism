.section .text._start, "ax"
.global _start
_start:
    msr SPsel, #1 // use SP_ELx (stack pointer for current EL)

    // Set up SP_EL2 (stack pointer for EL2)
    ldr   x0, =__stack_top // retrieve stack top from linker symbol
    mov   sp, x0
    // Install EL2 vector base
    adrp  x0, el2_vectors
    add   x0, x0, :lo12:el2_vectors
    msr   VBAR_EL2, x0 // set EL2 vector base address
    isb

    // Minimal SCTLR_EL2 setup. 
    // SCTLR_EL2 controls standard memory, system facilities, and provides status information for implemented functions.
    mrs   x0, SCTLR_EL2
    // leave as-is for now; we’ll manage MMU later
    msr   SCTLR_EL2, x0
    isb

    // Jump to C
    bl    el2_main

1:  wfi // wait for interrupt (hang here)
    b     1b

// Provide weak symbols from linker
.extern __stack_top