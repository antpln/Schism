#include "types.h"
#include "mmio.h"
#include "platform.h"

#define UART_DR      (UART0_BASE + 0x000)
#define UART_FR      (UART0_BASE + 0x018)
#define UART_IBRD    (UART0_BASE + 0x024)
#define UART_FBRD    (UART0_BASE + 0x028)
#define UART_LCRH    (UART0_BASE + 0x02C)
#define UART_CR      (UART0_BASE + 0x030)
#define UART_IMSC    (UART0_BASE + 0x038)
#define UART_ICR     (UART0_BASE + 0x044)

/* FR bits */
#define FR_TXFF      (1u << 5)

/* CR bits */
#define CR_UARTEN    (1u << 0)
#define CR_TXE       (1u << 8)

/* LCRH bits */
#define LCRH_FEN     (1u << 4)
#define LCRH_WLEN8   (3u << 5)

static inline void uart_putc(char c){
    /* wait while TX FIFO full */
    while (mmio_read32(UART_FR) & FR_TXFF) { }
    mmio_write32(UART_DR, (u32)c);
}

static inline void uart_puts(const char* s){
    while (*s){
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

static void uart_init(void){
    /* Disable, clear, program baud, 8N1, enable TX */
    mmio_write32(UART_CR, 0);
    mmio_write32(UART_ICR, 0x7FF);      // clear interrupts

    /* QEMU PL011 clock is typically 24MHz; set ~115200 baud: IBRD=13, FBRD=1 */
    mmio_write32(UART_IBRD, 13);
    mmio_write32(UART_FBRD, 1);
    mmio_write32(UART_LCRH, LCRH_WLEN8 | LCRH_FEN);
    mmio_write32(UART_IMSC, 0);         // no IRQs yet
    mmio_write32(UART_CR, CR_UARTEN | CR_TXE);
}

/* Expose a tiny interface for core/ */
void console_init(void){ uart_init(); }
void console_puts(const char* s){ uart_puts(s); }
void console_hex64(u64 x){
    const char* H="0123456789abcdef";
    char buf[2+16+1]; buf[0]='0'; buf[1]='x';
    for(int i=0;i<16;i++){ buf[2+15-i]=H[(x>>(i*4))&0xF]; }
    buf[18]='\0'; uart_puts(buf);
}
