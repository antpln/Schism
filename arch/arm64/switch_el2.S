.global vcpu_switch_asm
.type vcpu_switch_asm, %function
vcpu_switch_asm:
	// x0: pointer to trapframe_t
	ldr x1, [x0, #(8 * 31)]        // load SP_EL1 snapshot
	msr SP_EL1, x1               // restore SP_EL1
	ldr x1, [x0, #(8 * 32)]        // load ELR for guest resume
	msr ELR_EL2, x1          // restore ELR_EL2
	ldr x1, [x0, #(8 * 33)]        // load PSTATE for guest resume
	msr SPSR_EL2, x1    // restore SPSR_EL2
	isb

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
