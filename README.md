Schism
======

Schism is a learning-oriented AArch64 hypervisor that targets QEMU's
`virt` machine.  It expects to boot at EL2, brings up a minimal stage-1 MMU,
builds an identity-mapped stage-2 address space, and time-slices two toy guest
OSes that inhabit the guest physical window beginning at `0x4000_0000`.

What you get
------------
- **Deterministic EL2 bring-up.** `core/main.c` installs EL2 vectors, wipes
  `.bss`, enables the PL011-based console, programs the EL2 page tables, and
  creates the stage-2 view that the guests run inside.
- **Isolated guests behind stage-2.** `core/s2_mmu.c` builds per-VM slots with
  configurable guard pages so each guest receives a private carve-out of the
  `0x4000_0000` region.  The host can dump their shared memory mailboxes via
  `guest_shared_dump()`.
- **A preemptive VCPU scheduler.** `core/vcpu.c` saves/restores the trapframe,
  FP/SIMD context, pointer authentication keys, and VGIC list registers before
  bouncing between the guests.
- **Instrumented guest workloads.** `guests/counter_os.c` and
  `guests/memwalk_os.c` log architectural facts (EL, SP, private heap base,
  etc.) into shared slots and can report structured telemetry through the
  `hvc #0x60` hypercall handled in `core/trap.c`.

Repository layout
-----------------
- `arch/arm64/` – reset vector, EL2/EL1 vector tables, and the world switch
  assembly.
- `core/` – EL2 runtime, memory-management code, trap handler, and the VCPU
  scheduler.
- `drivers/` – PL011 UART console.
- `guests/` – the minimal guest OS payloads plus helper task code.
- `include/` – public headers shared between the host and the guests.
- `Makefile`, `linker.ld` – build logic and linker script for the flat image.

Prerequisites
-------------
Install a bare-metal AArch64 GCC and QEMU build that includes `qemu-system-aarch64`.
On most Linux distros:

```
sudo apt install gcc-aarch64-none-elf qemu-system-arm make
```

Building
--------

```
make            # produces build/schism.elf
```

The build uses the `aarch64-none-elf-` cross toolchain by default; override the
`CROSS` variable when invoking `make` if you use a different prefix.

Running under QEMU
------------------

```
make run
```

This launches `qemu-system-aarch64 -M virt,virtualization=on,gic-version=3` with
the generated ELF as the kernel.  UART output appears on the terminal because
we run QEMU in `-nographic` mode.  Expect the log to show EL2 bring-up followed
by periodic prints from the trap handler when guests invoke `hvc` or block in
`wfi`.

Development notes
-----------------
- **Memory map.** The linker starts the binary at `0x4000_0000` to match the
  stage-2 identity mapping.  Guard pages separate `.text`, `.rodata`, `.data`,
  `.bss`, and the EL2 stack.  Distinct PT_LOAD program headers keep the image
  non-RWX.
- **Guest layout.** `include/guest_layout.h` documents the shared mailbox
  region, per-guest private work buffers, and a virtual UART aperture that
  guests could consume for future experiments.
- **Hypercalls.** Guests call `guest_task_report()` which issues `hvc #0x60`;
  `core/trap.c` routes these to `handle_guest_task_report()` so EL2 can log the
  structured payloads or trigger world switches.
- **Diagnostics.** Invoke `guest_shared_dump()` from EL2 to inspect the shared
  slots that guests fill via `guest_log_value()`.  This is useful when running
  without hypercall logging enabled.

Long-term goals
---------------
- **Boot real guests.** Add a loader that can stage an external seL4 ELF/CPIO
  plus device tree into guest RAM and provide PSCI/SMCCC entry points so guests
  can hotplug CPUs or reset the system.
- **Dynamic stage-2 memory.** Replace the single identity slot with a general
  allocator that manages per-VM VMIDs, guard gaps, and MAP/UNMAP hypercalls so
  guests can grow/shrink their physical memory windows.
- **Full interrupt virtualization.** Emulate the GIC distributor/redistributor
  MMIO, virtual timers, SGIs, and maintenance interrupts so a guest OS can use
  its normal IRQ/timer drivers.
- **Device model coverage.** Expose or emulate the rest of QEMU virt’s devices
  (timer, GIC, VirtIO, PL031, etc.) and enforce access control per guest.
- **Real SMP scheduling.** Move from the toy round-robin loop to per-CPU vCPU
  contexts with PSCI CPU_ON/OFF handling and proper IPI injection so guests can
  control multiple cores.
- **Richer guest/host ABI.** Extend the trap handler into a generic hypercall
  dispatcher that handles PSCI, SMCCC, and fault forwarding instead of only the
  demo `hvc #0x60` path.
