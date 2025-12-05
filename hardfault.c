#include <stdint.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/exception.h"

__attribute__((naked))
void isr_hardfault(void) {
    __asm volatile(
        "mov r1, lr       \n"   // r1 = LR (EXC_RETURN)
        "movs r2, #4      \n"   // бит 2 = stack selection
        "tst r1, r2       \n"   // check bit2
        "beq 1f           \n"   // 0 -> MSP
        "mrs r0, psp      \n"   // r0 = fault stack frame (PSP)
        "b hardfault_c    \n"
        "1:               \n"
        "mrs r0, msp      \n"   // r0 = fault stack frame (MSP)
        "b hardfault_c    \n"
    );
}

//usage example arm-none-eabi-addr2line -e fw.elf 0x123456

void __attribute__((noinline)) hardfault_c(uint32_t *stack)
{
    uint32_t r0  = stack[0];
    uint32_t r1  = stack[1];
    uint32_t r2  = stack[2];
    uint32_t r3  = stack[3];
    uint32_t r12 = stack[4];
    uint32_t lr  = stack[5];
    uint32_t pc  = stack[6];
    uint32_t psr = stack[7];

    // Минимальный лог (UART обычно надёжнее USB в таком месте)
    printf("\n\n=== ERROR HardFault ===\n");
    printf(" R0  = 0x%08lx\n", (unsigned long)r0);
    printf(" R1  = 0x%08lx\n", (unsigned long)r1);
    printf(" R2  = 0x%08lx\n", (unsigned long)r2);
    printf(" R3  = 0x%08lx\n", (unsigned long)r3);
    printf(" R12 = 0x%08lx\n", (unsigned long)r12);
    printf(" LR  = 0x%08lx\n", (unsigned long)lr);
    printf(" PC  = 0x%08lx\n", (unsigned long)pc);
    printf(" PSR = 0x%08lx\n", (unsigned long)psr);
    printf("\n");

    volatile int i = 100000000;
    while (--i) {
    };

    watchdog_reboot(0, 0, 0);
    while (1) {        
    }
}

void __attribute__((naked)) isr_default_irq(void) {
    __asm volatile(
        "mrs r0, msp      \n"
        "b default_irq_c  \n"
    );
}

void default_irq_c(uint32_t *stack) {
    uint32_t pc  = stack[6];
    uint32_t psr = stack[7];

    printf("\n=== ERROR Unexpected IRQ ===\n");
    printf(" PC  = 0x%08lx\n", (unsigned long)pc);
    printf(" PSR = 0x%08lx\n", (unsigned long)psr);

    volatile int i = 10000000;
    while (--i) {
    };

    watchdog_reboot(0, 0, 0);
    while (1) {        
    }
}