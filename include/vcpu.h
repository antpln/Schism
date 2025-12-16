#include <stdbool.h>
#include "types.h"

// This structure holds the CPU state for a virtual CPU (VCPU) in the hypervisor.
typedef struct trapframe
{
    u64 regs[31]; // General-purpose registers x0-x30
    u64 sp_el1;   // Stack pointer for EL1
    u64 elr_el1;  // Exception Link Register for EL1
    u64 spsr_el1; // Saved Program Status Register for EL1
    u64 ttbr0_el1;   // Translation table base register 0
    u64 ttbr1_el1;   // Translation table base register 1
    u64 tcr_el1;     // Translation control register
    u64 sctlr_el1;   // System control register
    u64 tpidr_el1;   // Thread pointer ID register
    u64 cntkctl_el1; // Timer control register for EL1/EL0 access
    u64 cntp_ctl_el0;  // Physical timer control (virtualized view)
    u64 cntp_cval_el0; // Physical timer compare value stored in virtual counts
    u64 cntv_ctl_el0;  // Virtual timer control
    u64 cntv_cval_el0; // Virtual timer compare value
} trapframe_t;


// Architecture-specific state for a VCPU
typedef struct vcpu_arch
{
    u64 vttbr_el2;   // Virtualization Translation Table Base Register for EL2
    u64 cntvoff_el2; // Counter-timer Virtual Offset Register for EL2
    u64 cntvct_el0;  // Last virtual counter snapshot to freeze time when descheduled

    // Feature blocks
    struct
    {
        u8 used;               // Non-zero once the SIMD state is captured
        u32 fpcr;              // Floating-point control register (FPCR)
        u32 fpsr;              // Floating-point status register (FPSR)
        u64 vregs[32][2];      // Q0-Q31, two 64-bit lanes per 128-bit register
    } fp; // Floating Point and SIMD state
    struct {
        u8 used;
    } sve; // Scalable Vector Extension

    struct {
        u8 used;
        u64 apia, apib, apda, apdb; // Pointer Authentication Keys
    } pauth; // Pointer Authentication

    struct {
        u64 lrs[16]; // List Registers for Virtualization
        u32 vmcr;   // Virtualization Miscellaneous Control Register
        u32 apr;   // Active Priority Register (AP0R0) for the VGIC
    } vgic; // Virtual Generic Interrupt Controller

    trapframe_t tf; // Guest register state
} vcpu_arch_t;


// Main VCPU structure
typedef struct vcpu
{
    vcpu_arch_t arch;
    struct sch_vm *vm; // Back-reference to parent VM
    int vcpu_id;   // VCPU identifier within the VM
    void (*kresume)(struct vcpu*); // Kernel resume function pointer
    u64 kresume_arg0, kresume_arg1; // Arguments for kresume
    bool request_yield; // Flag to request a yield after a trap
} vcpu_t;

extern trapframe_t *current_trapframe;

void vcpu_scheduler_register(vcpu_t* vcpu);
void vcpu_scheduler_set_current(vcpu_t* vcpu);
vcpu_t* vcpu_scheduler_current(void);
bool vcpu_scheduler_yield(void);
void vcpu_run(vcpu_t* vcpu);
void world_switch(vcpu_t *from, vcpu_t *to);

extern void guest_el1_vectors(void);
