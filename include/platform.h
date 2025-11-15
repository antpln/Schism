#pragma once

// QEMU virt board peripherals we care about.
#define UART0_BASE      0x09000000ull
#define UART_SIZE       0x1000ull
#define UART_PA         UART0_BASE

#define VIRT_PMU_BASE   0x09010000ull
#define VIRT_PMU_SIZE   0x1000ull
