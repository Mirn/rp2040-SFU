// rp2040_quiesce_and_jump.h
#pragma once
#include <stdint.h>
#include "RP2040.h"
#include "pico/stdlib.h"
#include "pico/platform.h"
#include "pico/multicore.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/resets.h"
#include "hardware/regs/xip.h"
#include "hardware/regs/clocks.h"
#include "hardware/regs/pll.h"
#include "hardware/structs/scb.h"
#include "hardware/structs/clocks.h"
#include "hardware/structs/pll.h"
#include "hardware/structs/dma.h"
#include "hardware/structs/watchdog.h"
#include "hardware/structs/uart.h"
#include "hardware/structs/i2c.h"
#include "hardware/structs/spi.h"
#include "hardware/structs/pio.h"
#include "hardware/structs/pwm.h"
#include "hardware/structs/resets.h"
#include "hardware/structs/timer.h"
#include "hardware/structs/usb.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/platform_defs.h"
#include "hardware/sync.h"
#include "hardware/dma.h"
#include "hardware/pll.h"
#include "hardware/flash.h"

static void __no_inline_not_in_flash_func(reset_selected_peripherals)(void) {
    const uint32_t mask =
        RESETS_RESET_ADC_BITS   |
        RESETS_RESET_DMA_BITS   |
        RESETS_RESET_PWM_BITS   |
        RESETS_RESET_PIO0_BITS  |
        RESETS_RESET_PIO1_BITS  |
        RESETS_RESET_RTC_BITS   |
        RESETS_RESET_SPI0_BITS  |
        RESETS_RESET_SPI1_BITS  |
        RESETS_RESET_TIMER_BITS | 
        RESETS_RESET_UART0_BITS |
        RESETS_RESET_UART1_BITS |
        RESETS_RESET_USBCTRL_BITS;    

    resets_hw->reset |= mask;
    for (volatile int i = 0; i < 1000; i++) {     
    }    
    resets_hw->reset &= ~mask;
    
    volatile uint32_t timeout = 100000;
    while (((resets_hw->reset_done & mask) != mask) && (timeout != 0)) {
        timeout -= 1;
    }
}

static void __no_inline_not_in_flash_func(cleanup_caches)(void) {
    xip_ctrl_hw->ctrl = XIP_CTRL_EN_BITS;
    xip_ctrl_hw->flush = 1;
    volatile uint32_t timeout = 1000000;
    while ((!(xip_ctrl_hw->stat & XIP_STAT_FLUSH_READY_BITS)) && (timeout != 0)) {
        timeout -= 1;
    }    
    __DSB(); 
    __ISB();
}

static void __no_inline_not_in_flash_func(quiesce_peripherals)(void) {
    for (int gpio = 0; gpio <= 29; ++gpio) {
        if (gpio == 24 || gpio == 25) continue; //keep SWD pins
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        gpio_set_dir(gpio, false);    
        gpio_disable_pulls(gpio);
    }

    __disable_irq();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk;
    SCB->SHCSR = 0; 

    NVIC->ICER[0] = 0xFFFFFFFFu;  // disable all
    NVIC->ICPR[0] = 0xFFFFFFFFu;  // clear pending
#ifdef NVIC_ICER1
    NVIC->ICER[1] = 0xFFFFFFFFu;
    NVIC->ICPR[1] = 0xFFFFFFFFu;
#endif

#ifdef NVIC_ICER2
#error "TODO: Add NVIC_ICER2 and etc into quiesce_peripherals"
#endif

    multicore_reset_core1();
    __DSB(); __ISB();    
    volatile uint32_t timeout = 100000;
    while ((sio_hw->fifo_st & SIO_FIFO_ST_VLD_BITS) && (timeout != 0)) {
        (void)sio_hw->fifo_rd;
        timeout -= 1;
    }
    multicore_fifo_clear_irq();    
    sio_hw->fifo_st = SIO_FIFO_ST_VLD_BITS | SIO_FIFO_ST_RDY_BITS;
    __DSB(); __ISB();    

    cleanup_caches();

    watchdog_hw->ctrl = 0;
    __DSB(); __ISB();    
    (void)watchdog_hw->ctrl;

    timer_hw->inte = 0;
    timer_hw->intr = 0xFFFFFFFFu;

    for (int ch = 0; ch < NUM_DMA_CHANNELS; ++ch) {
        dma_hw->abort = 1u << ch;
        int timeout = 1000; //10ms

        while (dma_channel_is_busy(ch) && (timeout--)) {
            for (volatile int i = 0; i < 1000; i++) {     
            }
        }
    }
    dma_hw->inte0 = 0; 
    dma_hw->inte1 = 0;
    dma_hw->ints0 = dma_hw->ints0;   
    dma_hw->ints1 = dma_hw->ints1;

    for (int i = 0; i < 2; ++i) {
        uart_hw_t *u = (i == 0) ? uart0_hw : uart1_hw;
        u->imsc  = 0;
        u->icr   = 0x7FF;               // clear all pending
        u->dmacr = 0;                   // switch off DMA
        u->cr   &= ~(UART_UARTCR_RXE_BITS | UART_UARTCR_TXE_BITS | UART_UARTCR_UARTEN_BITS);
        // FIFO off 
        uint32_t lcr = u->lcr_h & ~UART_UARTLCR_H_FEN_BITS;
        u->lcr_h = lcr;
        u->rsr   = 0xFFFFFFFFu;         // clear sticky errors
    }

    i2c0_hw->intr_mask = 0; 
    (void)i2c0_hw->clr_intr; 

    i2c1_hw->intr_mask = 0; 
    (void)i2c1_hw->clr_intr;

    spi0_hw->imsc = 0; 
    (void)spi0_hw->mis;

    spi1_hw->imsc = 0; 
    (void)spi1_hw->mis;

    //PIO
    for (int i = 0; i < 2; ++i) {
        pio_hw_t *p = (i == 0) ? pio0_hw : pio1_hw;
        p->inte0 = 0; 
        p->inte1 = 0;
        p->irq = p->irq;              
        p->ctrl = 0;                  
        p->fdebug = 0xFFFFFFFFu;      // clear FIFO debug flags
    }

    pwm_hw->intr = 0xFFFFu;
    pwm_hw->inte = 0;
    for (int s = 0; s < 8; ++s) {
        pwm_hw->slice[s].csr = 0;     // disable slice
    }

#ifdef USBCTRL_BASE
    usb_hw->sie_ctrl = 0;
    usb_hw->inte = 0;
    (void)usb_hw->ints; 
#endif
    reset_selected_peripherals();
}

__attribute__((noreturn)) 
static  void __no_inline_not_in_flash_func(context_switch_to_vtor)(uint32_t VTOR_ptr) {
    uint32_t new_sp = *(uint32_t *)(VTOR_ptr + 0);
    uint32_t new_pc = *(uint32_t *)(VTOR_ptr + 4);

    SCB->VTOR = VTOR_ptr;
    
    __DSB(); __ISB();
    __enable_irq();
	__set_MSP(new_sp);
	(*(void (*)())(new_pc))();
    while (1) {        
    }; //avoid warining "'noreturn' function does return"
}

//external func
__attribute__((noreturn))
static void __no_inline_not_in_flash_func(rp2040_quiesce_and_jump)(uint32_t VTOR_ptr) {
    quiesce_peripherals();
    context_switch_to_vtor(VTOR_ptr);
}
