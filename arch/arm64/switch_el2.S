.global vcpu_switch_asm
.type vcpu_switch_asm, %function
vcpu_switch_asm:
	// x0: pointer to trapframe_t (guest state destination/source)

	// Save host callee-saved registers x19-x30 and SP to host_saved_area
	// so the EL2 vector handler can restore them and return to C.
	adrp x2, host_saved_area
	add x2, x2, :lo12:host_saved_area
	stp x19, x20, [x2, #0]
	stp x21, x22, [x2, #16]
	stp x23, x24, [x2, #32]
	stp x25, x26, [x2, #48]
	stp x27, x28, [x2, #64]
	stp x29, x30, [x2, #80]
	mov x3, sp
	str x3, [x2, #96]

	// Restore guest EL1 stack pointer, ELR_EL2 and SPSR_EL2 from trapframe
	ldr x1, [x0, #(8 * 31)]        // load SP_EL1 snapshot
	msr SP_EL1, x1               // restore SP_EL1
	ldr x1, [x0, #(8 * 32)]        // load ELR for guest resume
	msr ELR_EL2, x1          // restore ELR_EL2
	ldr x1, [x0, #(8 * 33)]        // load PSTATE for guest resume
	msr SPSR_EL2, x1    // restore SPSR_EL2
	isb

	ldp x1, x2, [x0, #(8 * 34)]    // restore TTBR0/1_EL1
	msr TTBR0_EL1, x1
	msr TTBR1_EL1, x2
	ldp x1, x2, [x0, #(8 * 36)]    // restore TCR_EL1 and SCTLR_EL1
	msr TCR_EL1, x1
	msr SCTLR_EL1, x2
	ldp x1, x2, [x0, #(8 * 38)]    // restore TPIDR_EL1 and CNTKCTL_EL1
	msr TPIDR_EL1, x1
	msr CNTKCTL_EL1, x2
	ldp x1, x2, [x0, #(8 * 40)]    // restore CNTP_CTL/CVAL_EL0 (CVAL is virtual)
	mrs x3, CNTVOFF_EL2
	sub x2, x2, x3                 // convert virtual CVAL to physical counter domain
	and x1, x1, #0x3
	msr CNTP_CVAL_EL0, x2
	msr CNTP_CTL_EL0, x1
	ldp x1, x2, [x0, #(8 * 42)]    // restore CNTV_CTL/CVAL_EL0
	and x1, x1, #0x3
	msr CNTV_CVAL_EL0, x2
	msr CNTV_CTL_EL0, x1
	isb

	// Restore guest general-purpose registers x1..x30 from trapframe
	ldp x1, x2, [x0, #(8 * 1)]        // restore general-purpose registers
	ldp x3, x4, [x0, #(8 * 3)]
	ldp x5, x6, [x0, #(8 * 5)]
	ldp x7, x8, [x0, #(8 * 7)]
	ldp x9, x10, [x0, #(8 * 9)]
	ldp x11, x12, [x0, #(8 * 11)]
	ldp x13, x14, [x0, #(8 * 13)]
	ldp x15, x16, [x0, #(8 * 15)]
	ldp x17, x18, [x0, #(8 * 17)]
	ldp x19, x20, [x0, #(8 * 19)]
	ldp x21, x22, [x0, #(8 * 21)]
	ldp x23, x24, [x0, #(8 * 23)]
	ldp x25, x26, [x0, #(8 * 25)]
	ldp x27, x28, [x0, #(8 * 27)]
	ldp x29, x30, [x0, #(8 * 29)]

	ldr x0, [x0, #(8 * 0)]         // guest x0 last so pointer is no longer needed
	eret

.size vcpu_switch_asm, . - vcpu_switch_asm // function size (for debugging)
