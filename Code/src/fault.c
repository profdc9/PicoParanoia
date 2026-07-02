// fault.c — Cortex-M0+ HardFault reporter.
//
// Overrides the Pico SDK's weak isr_hardfault. On a fault it recovers the
// exception stack frame ({R0,R1,R2,R3,R12,LR,PC,xPSR}) and prints the key
// registers to the TV console, then halts. Look the PC up with:
//   arm-none-eabi-addr2line -e build/picoparanoia.elf <PC>
// Diagnostic aid; harmless to leave in.

#include <stdint.h>
#include "pico/stdlib.h"
#include "consoleio.h"

static void put_hex32(uint32_t v)
{
    static const char h[] = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4) console_putch(h[(v >> i) & 0xF]);
}

// frame[0..7] = R0, R1, R2, R3, R12, LR, PC, xPSR
void hardfault_report(uint32_t *frame)
{
    console_clrscr();
    console_puts("*** HARD FAULT ***\r\n\r\n");
    console_puts("PC="); put_hex32(frame[6]);
    console_puts("\r\nLR="); put_hex32(frame[5]);
    console_puts("\r\n\r\nR0="); put_hex32(frame[0]);
    console_puts(" R1="); put_hex32(frame[1]);
    console_puts("\r\nR2="); put_hex32(frame[2]);
    console_puts(" R3="); put_hex32(frame[3]);
    console_puts("\r\n\r\naddr2line the PC.");
    for (;;) tight_loop_contents();
}

// Pick the active stack pointer (MSP/PSP per EXC_RETURN bit 2) and tail-call the
// C reporter with it in r0.
__attribute__((naked)) void isr_hardfault(void)
{
    __asm volatile(
        "movs r0, #4              \n"
        "mov  r1, lr              \n"
        "tst  r0, r1              \n"
        "beq  1f                  \n"
        "mrs  r0, psp             \n"
        "b    2f                  \n"
        "1:                       \n"
        "mrs  r0, msp             \n"
        "2:                       \n"
        "ldr  r1, =hardfault_report \n"
        "bx   r1                  \n"
    );
}
