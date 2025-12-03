#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/uart.h"

#define UART_ID uart0
#define BAUD_RATE 921600
#define UART_TX_PIN 0
#define UART_RX_PIN 1

uint8_t rx_buffer[0x20000];
uint32_t rx_pos_write = 0;
uint32_t rx_pos_read  = 0;

uint32_t rx_errors = 0;
uint32_t rx_overfulls = 0;
uint32_t rx_count_max = 0;
uint32_t rx_total = 0;

#define RX_DMA_NOT_INITED UINT32_MAX
#define RX_INITIAL UINT32_MAX
#define RX_DMA_SIZE 32768
static uint8_t __attribute__((aligned(RX_DMA_SIZE))) rxDMAbuf[RX_DMA_SIZE];
static int rxch = RX_DMA_NOT_INITED;
static uint32_t rx_dma_old_cnt = 0;
static uint32_t rx_dma_old_pos = 0;


static void uart_flush_rx_fifo(uart_inst_t *u) {
    // stop temporary receive and transmit
    uart_get_hw(u)->cr &= ~(UART_UARTCR_RXE_BITS | UART_UARTCR_TXE_BITS | UART_UARTCR_UARTEN_BITS);
    __compiler_memory_barrier();

    // Disable FIFO and auto-clean
    uint32_t lcr = uart_get_hw(u)->lcr_h;
    uart_get_hw(u)->lcr_h = lcr & ~UART_UARTLCR_H_FEN_BITS; // FEN=0
    __compiler_memory_barrier();

    uint32_t timeout = 128;
    while ((!(uart_get_hw(u)->fr & UART_UARTFR_RXFE_BITS)) && (timeout--)) {
        (void)uart_get_hw(u)->dr;
    }
    
    uart_get_hw(u)->rsr = 0xFFFFFFFF; // ECR alias

    //enable UART back
    uart_get_hw(u)->lcr_h = lcr | UART_UARTLCR_H_FEN_BITS; // FEN=1
    uart_get_hw(u)->cr |=  (UART_UARTCR_RXE_BITS | UART_UARTCR_TXE_BITS | UART_UARTCR_UARTEN_BITS);
}

void usart_init() {
    rx_dma_old_cnt = 0;
    rx_dma_old_pos = 0;
    rx_pos_write   = 0;
    rx_pos_read    = 0;
    rx_errors      = 0;
    rx_overfulls   = 0;
    rx_count_max   = 0;
    rx_total       = 0;

    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);   
    uart_set_format(UART_ID, 8 /*data*/, 1 /*stop*/, UART_PARITY_NONE);
    //uart_set_dma_enabled(UART_ID, true);
    uart_set_fifo_enabled(UART_ID, true);

    uart_flush_rx_fifo(UART_ID);

    rxch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(rxch);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_dreq(&c, DREQ_UART0_RX);
    
    channel_config_set_ring(&c, /*write=*/true, /*ring_bits=*/15);

    dma_channel_configure(rxch, &c, rxDMAbuf, &uart0_hw->dr, RX_INITIAL, true);
}

void usart_deinit(void) {
    // stop DMA channel properly with timeout
    if (rxch != RX_DMA_NOT_INITED) {
        dma_channel_abort(rxch);
        int timeout = 100 * 10; //100ms
        sleep_us(10);
        while (dma_channel_is_busy(rxch) && (timeout--)) {
            sleep_us(100);
        }
        dma_channel_unclaim(rxch);
        rxch = RX_DMA_NOT_INITED;
    }

    uart_get_hw(UART_ID)->dmacr = 0; //Disable DMA requests from UART

    // stop UART: disable RX/TX/UARTEN
    uart_get_hw(UART_ID)->cr &= ~(UART_UARTCR_RXE_BITS | UART_UARTCR_TXE_BITS | UART_UARTCR_UARTEN_BITS);
    __compiler_memory_barrier();

    // turn off FIFO (and clear) + reset errors
    uint32_t lcr = uart_get_hw(UART_ID)->lcr_h & ~UART_UARTLCR_H_FEN_BITS;
    uart_get_hw(UART_ID)->lcr_h = lcr;
    uart_get_hw(UART_ID)->rsr = 0xFFFFFFFF;

    // reset all IRG masks
    uart_get_hw(UART_ID)->imsc = 0;
    uart_get_hw(UART_ID)->icr  = 0x7FF; // reset all pending IRQ

    // (UART GPIO input into Hi-Z)
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_SIO);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(UART_TX_PIN, false);
    gpio_set_dir(UART_RX_PIN, false);
    gpio_disable_pulls(UART_TX_PIN);
    gpio_disable_pulls(UART_RX_PIN);

    uart_deinit(UART_ID);
}

void send(const uint8_t tx_data) {
    uart_write_blocking(UART_ID, &tx_data, sizeof(tx_data));
}

void send_block(const uint8_t *data, const uint32_t size)
{
    uart_write_blocking(UART_ID, data, size);
}

void send_str(const char *str)
{
    uart_write_blocking(UART_ID, str, strlen(str));
}

void rx_dma_check() {
    
    size_t rx_cnt_new = RX_INITIAL - dma_hw->ch[rxch].transfer_count;
    uint32_t cnt = rx_cnt_new - rx_dma_old_cnt;
    rx_dma_old_cnt = rx_cnt_new;
    if (cnt > RX_DMA_SIZE) {
        rx_overfulls += (cnt - RX_DMA_SIZE);
        cnt = cnt & (RX_DMA_SIZE - 1);
        send_str("OverDMA!\r");
    }

    uint32_t rsr = uart_get_hw(UART_ID)->rsr &0xFF; // only OE/BE/PE/FE errors
    if (rsr != 0) {
        uart_get_hw(UART_ID)->rsr = rsr;
        rx_errors += 1;
    }

    if (cnt == 0) {
        return;
    }
    rx_total += cnt;

    while (cnt--) {
		rx_buffer[rx_pos_write % sizeof(rx_buffer)] = rxDMAbuf[rx_dma_old_pos];
		rx_pos_write++;
        rx_dma_old_pos = (rx_dma_old_pos + 1 ) & (RX_DMA_SIZE - 1);
    }
}

uint32_t receive_count()
{
    rx_dma_check();
	return rx_pos_write - rx_pos_read;
}

bool receive_byte(uint8_t *rx_data)
{
    rx_dma_check();
	if (rx_pos_read == rx_pos_write) return false;

	uint32_t count = receive_count();
	if (rx_count_max < count)
		rx_count_max = count;

	if (count >= sizeof(rx_buffer))
	{
		rx_pos_read = rx_pos_write + 1 - sizeof(rx_buffer);
		rx_overfulls++;
		send_str("Over!\r");
	}

	*rx_data = rx_buffer[rx_pos_read % sizeof(rx_buffer)];
	rx_pos_read++;
	return true;
}

uint32_t receive_size()
{
	return sizeof(rx_buffer);
}
