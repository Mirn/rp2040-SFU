#ifndef __USART_MINI_H__
#define __USART_MINI_H__

#ifdef STM32F10X_LD_VL
#include "stm32f10x_usart.h"
#endif

#ifdef STM32F4XX
#include "stm32f4xx_usart.h"
#endif

// FOR RP2040

#define BAUD_RATE_DEFAULT 921600

extern uint32_t rx_errors;
extern uint32_t rx_overfulls;
extern uint32_t rx_count_max;
extern uint32_t rx_total;


void usart_init();
void usart_deinit();

void send_str(const char *str);
void send(const uint8_t tx_data);

bool receive_byte(uint8_t *rx_data);
uint32_t receive_count();
uint32_t receive_size();

void send_block(const uint8_t *data, const uint32_t size);

void rx_dma_check();

uint32_t usart_get_baud(void);
bool usart_set_baud(uint32_t new_baud);

#endif
