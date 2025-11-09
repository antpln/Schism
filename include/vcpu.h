#include "types.h"


// This structure holds the CPU state for a virtual CPU (VCPU) in the hypervisor.
typedef struct trapframe
{
    u64 regs[31]; // General-purpose registers x0-x30
    u64 sp_el1;   // Stack pointer for EL1
    u64 elr_el1;  // Exception Link Register for EL1
    u64 spsr_el1; // Saved Program Status Register for EL1
} trapframe_t;


// Architecture-specific state for a VCPU
typedef struct vcpu_arch
{
    u64 vttbr_el2;   // Virtualization Translation Table Base Register for EL2
    u64 cntvoff_el2; // Counter-timer Virtual Offset Register for EL2

    // Feature blocks
    struct
    {
        u8 used;               // Non-zero once the SIMD state is captured
        u32 fpcr;              // Floating-point control register (FPCR)
        u32 fpsr;              // Floating-point status register (FPSR)
        u64 vregs[32][2];      // Q0-Q31, two 64-bit lanes per 128-bit register
    } fp; // Floating Point and SIMD state
    struct {
        u8 used; // TODO
    } sve; // Scalable Vector Extension

    struct {
        u8 used;
        u64 apia, apib, apda, apdb; // Pointer Authentication Keys
    } pauth; // Pointer Authentication

    struct {
        u64 lrs[16]; // List Registers for Virtualization
        u32 vmcr;   // Virtualization Miscellaneous Control Register
        u32 apr;   // Virtualization Address Space Identifier Register
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
} vcpu_t;

extern trapframe_t *current_trapframe;