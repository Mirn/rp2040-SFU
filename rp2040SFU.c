#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"

#include "usart_mini.h"
#include "packet_receiver.h"
#include "sfu_commands.h"
#include "crc32.h"

int main() {
    stdio_init_all();
    usart_init();
    crc32_init_table();
    crc32_IEEE8023_init();
    //sleep_ms(5000);

	receive_packets_init();
	sfu_command_init();

    bool variant = false;
    bool main_valid = find_latest_variant(&variant);
    main_selector = variant;

	while (1)
	{
		stat_error_timeout = 0;
		while ((stat_error_timeout * PACKET_TIMEOUT_mS) < 2000)
		{
			receive_packets_worker();
			receive_packets_print_stat();
		}

        if (main_update_started) {
            main_update_started = false;
            main_valid = find_latest_variant(&variant);
            main_selector = variant;
        }

        if (main_valid) {
            main_start();
        }
	}

    // while (true) {
    //     uint8_t rx = 0;
    //     while (receive_byte(&rx)) {
    //         printf("%02X ", rx);
    //     }

    //     printf("%i\t%i\t%i\t\t%i\n", rx_errors, rx_overfulls, rx_count_max, rx_total);
    //     send_str("Hello, UART!\n");
    //     sleep_ms(1000);
    // }
}
