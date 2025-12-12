/*
 * sfu_commands.c
 *
 *  Created on: 08 ���� 2016 �.
 *      Author: Easy
 */

#if __has_include("pico/version.h")
#define USING_PICO_SDK
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "crc32.h"
#include "rp2040_quiesce_and_jump.h"
#include "bin2page_encoder/decoder/bin2page_decoder.h"

#define UNUSED_A __attribute__ ((unused))
#define CCMDATARAM_BASE SRAM_BASE
typedef struct {
    uint32_t IDCODE;
} tDBGMCU;

const tDBGMCU _dbgmcu = {0x20402040};
const tDBGMCU * DBGMCU = &_dbgmcu;
const uint8_t DEVICE_ID_BLOCK_PTR[12] = "RP2040_2Page";

//#define FLASH_SIZE          (PICO_FLASH_SIZE_BYTES/1024)
#define FLASH_SIZE          ((2 * 1024 * 1024)/1024)

#define SELECTOR_A (false)
#define SELECTOR_B (true)
bool main_selector = false;
bool main_update_started = false;
uint32_t max_time_stamp = 0;
uint32_t fw_fullbody_crc32 = UINT32_MAX;

#define FLASH_BASE          XIP_BASE
#define BOOTLOADER_SIZE     0x10000
#define BOOTLOADER_FROM    (FLASH_BASE)
#define BOOTLOADER_TO      (FLASH_BASE + BOOTLOADER_SIZE)
#define MAIN_START_FROM    (BOOTLOADER_TO + 0x00200000 * (main_selector & 0x01))
#define MAIN_RUN_FROM      (MAIN_START_FROM + 0x100)
#define MAIN_END           (MAIN_START_FROM + FLASH_SIZE_CORRECT*1024)

#define NV_PARAM_BLOCK_OFFSET (BOOTLOADER_TO - FLASH_SECTOR_SIZE)
#define NV_PARAM_BLOCK      ((uint8_t *)NV_PARAM_BLOCK_OFFSET)
#define NV_PARAM_BLOCK_SIZE FLASH_PAGE_SIZE
#define FIRST_START_OFFSET  0
#define FIRST_START         NV_PARAM_BLOCK[FIRST_START_OFFSET]

#define MAIN_CRC_OFFSET           (0)
#define MAIN_TIME_STAMP_OFFSET    (1)
#define MAIN_CRC           ((uint32_t *)(MAIN_END + sizeof(uint32_t) * MAIN_CRC_OFFSET))
#define MAIN_TIME_STAMP    ((uint32_t *)(MAIN_END + sizeof(uint32_t) * MAIN_TIME_STAMP_OFFSET))

#define FLASH_SIZE_CORRECT   (((FLASH_SIZE*1024 - BOOTLOADER_SIZE) / 1024) - (FLASH_SECTOR_SIZE/ 1024))
#define FLASH_SIZE_CORRECT_L (FLASH_SIZE_CORRECT & 0xFF)
#define FLASH_SIZE_CORRECT_H (FLASH_SIZE_CORRECT >> 8)
#else

#include "stm32kiss.h"
#include "hw_init.h"
#endif
#include "usart_mini.h"
#include "packet_receiver.h"

#ifdef USE_STDPERIPH_DRIVER
#include "stm32f4xx_flash.h"
#include "stm32f4xx_crc_inline.h"
#include "stm32f4xx_flash_inline.h"
#endif

#define SFU_VER 0x0100

#define SFU_CMD_ERASE_PART   0xB3
#define SFU_CMD_INFO    0x97
#define SFU_CMD_ERASE   0xC5
#define SFU_CMD_WRITE   0x38
#define SFU_CMD_START   0x26
#define SFU_CMD_TIMEOUT 0xAA
#define SFU_CMD_WRERROR 0x55
#define SFU_CMD_HWRESET 0x11

bool check_first_start_once();
bool check_run_context(uint32_t *boot_from);
void erase_sign_block();
void add_sign_crc();

static void sfu_command_info(uint8_t code, uint8_t *body, uint32_t size);
static void sfu_command_erase(uint8_t code, uint8_t *body, uint32_t size);
static void sfu_command_write(uint8_t code, uint8_t *body, uint32_t size);
static void sfu_command_start(uint8_t code, uint8_t *body, uint32_t size);

static uint32_t write_addr = 0;
static uint32_t write_addr_real = 0;
static bool write_error = false;

bool find_latest_variant(bool *variant) {
    printf("\nfind_latest_variant\n");

    if (check_first_start_once()) {
        main_selector = false;
        *variant = main_selector;
        if (!check_run_context((uint32_t*)MAIN_RUN_FROM)) {
            send_str("Fisrt start, context erorr, no any variant\n");
            return false;
        } else {
            add_sign_crc();
            return true;
        }
    }
    
    bool old_selector = main_selector;
    main_selector = SELECTOR_A;
    uint32_t tstamp_a = *MAIN_TIME_STAMP;
    uint32_t crc32_a  = *MAIN_CRC;
    main_selector = SELECTOR_B;
    uint32_t tstamp_b = *MAIN_TIME_STAMP;
    uint32_t crc32_b  = *MAIN_CRC;

    printf("tstamp_a\t%08X\n", tstamp_a);
    printf("tstamp_b\t%08X\n", tstamp_b);
    printf("crc32_a \t%08X\n", crc32_a);
    printf("crc32_b \t%08X\n", crc32_b);
    printf("\n");

    max_time_stamp = 0;
    bool tstamp_a_ok = (tstamp_a != UINT32_MAX);
    bool tstamp_b_ok = (tstamp_b != UINT32_MAX);

    if (tstamp_a_ok && tstamp_b_ok) {
        if (tstamp_a >= tstamp_b) {
            send_str("Check Variant A: ");
            max_time_stamp = tstamp_a;
            main_selector = SELECTOR_A;
        } else {
            send_str("Check Variant B: ");
            max_time_stamp = tstamp_b;
            main_selector = SELECTOR_B;
        }
        if (crc32_IEEE8023((const void *)MAIN_START_FROM, MAIN_END-MAIN_START_FROM) == (*MAIN_CRC)) {
            send_str("GOOD, selected from two\n");
            *variant = main_selector;
            main_selector = old_selector;
            return true;
        }
        main_selector = !main_selector;
        send_str("Broken\n  ...Check another Variant: ");
        if (crc32_IEEE8023((const void *)MAIN_START_FROM, MAIN_END-MAIN_START_FROM) == (*MAIN_CRC)) {
            send_str("GOOD, selected from two\n");
            *variant = main_selector;
            main_selector = old_selector;
            return true;
        }
        send_str("Broken also\n");
        main_selector = old_selector;
        return false;
    }

    if (tstamp_a_ok) {
        max_time_stamp = tstamp_a;
        main_selector = SELECTOR_A;
        if (crc32_IEEE8023((const void *)MAIN_START_FROM, MAIN_END-MAIN_START_FROM) == (*MAIN_CRC)) {
            send_str("Variant A selected as single\n");
            *variant = main_selector;
            main_selector = old_selector;
            return true;
        } else {
            send_str("Variant A is single, but also broken\n");
            main_selector = old_selector;
            return false;
        }
    }

    if (tstamp_b_ok) {
        max_time_stamp = tstamp_b;
        main_selector = SELECTOR_B;
        if (crc32_IEEE8023((const void *)MAIN_START_FROM, MAIN_END-MAIN_START_FROM) == (*MAIN_CRC)) {
            send_str("Variant B selected as single\n");
            *variant = main_selector;
            main_selector = old_selector;
            return true;
        } else {
            send_str("Variant B is single, but also broken\n");
            main_selector = old_selector;
            return false;
        }
    }
    send_str("No any variant\n");
    main_selector = old_selector;
    return false;    
}


bool check_run_context(uint32_t *boot_from) {
    // printf("Context stack: %08X\n", boot_from[0]);
    // printf("Context flash: %08X\n", boot_from[1]);
	if (((boot_from[0] >> 24) != (CCMDATARAM_BASE >> 24)) && (
            (boot_from[0] <= (SRAM_BASE)) ||
            (boot_from[0] >  (SRAM_END))
        )||((boot_from[0] & 0x00000003) != 0)) //stack must be aligned to 4
    {
        send_str("Bootloader: STACK CONTEXT ERROR\n");
		return false;
    }

	if ((boot_from[1] <= MAIN_RUN_FROM) ||
        (boot_from[1] >= MAIN_END) || 
        ((boot_from[1] & 0x00000001) == 0))      //check if non thumb2 mode
    {
        send_str("Bootloader: FLASH CONTEXT ERROR\n");
        return false;
    }
    return true;
}

void sfu_command_init()
{
	uint32_t temp;
	packet_send(SFU_CMD_HWRESET, (void*)&temp, 0);
}

void sfu_command_timeout()
{
	if (write_addr == 0) return;
	write_addr = 0;
    write_addr_real = 0;
    write_error = false;
    main_update_started = false;
	packet_send(SFU_CMD_TIMEOUT, (uint8_t*)&write_addr, sizeof(write_addr));
}

void sfu_command_parser(uint8_t code, uint8_t *body, uint32_t size)
{
	if (code == SFU_CMD_INFO)  sfu_command_info(code, body, size);
	if (code == SFU_CMD_ERASE) sfu_command_erase(code, body, size);
	if (code == SFU_CMD_WRITE) sfu_command_write(code, body, size);
	if (code == SFU_CMD_START) sfu_command_start(code, body, size);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void serialize_uint32(uint8_t *body, uint32_t value)
{
#if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
	*((uint32_t *)body) = value;
#else
	body[0] = (value >>  0) & 0xFF;
	body[1] = (value >>  8) & 0xFF;
	body[2] = (value >> 16) & 0xFF;
	body[3] = (value >> 24) & 0xFF;
#endif
}

static inline void serialize_uint16(uint8_t *body, uint16_t value)
{
#if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
	*((uint16_t *)body) = value;
#else
	body[0] = (value >>  0) & 0xFF;
	body[1] = (value >>  8) & 0xFF;
#endif
}

static inline uint32_t deserialize_uint32(uint8_t *body)
{
#if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
	return *((uint32_t *)body);
#else
	return  ((uint32_t)body[0] <<  0) |
			((uint32_t)body[1] <<  8) |
			((uint32_t)body[2] << 16) |
			((uint32_t)body[3] << 24);
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void sfu_command_info(uint8_t code, UNUSED_A uint8_t *body, UNUSED_A uint32_t size)
{
    main_selector = !main_selector;
	const uint32_t CPU_TYPE = DBGMCU->IDCODE & 0xFFFF0FFF; //7..4 bits reserved

	for (uint32_t index = 0; index < 12; index++)
		body[index] = DEVICE_ID_BLOCK_PTR[index];

	serialize_uint32(body + 12, CPU_TYPE);
	serialize_uint16(body + 16, FLASH_SIZE_CORRECT);
	serialize_uint16(body + 18, SFU_VER);
	serialize_uint32(body + 20, receive_size());
	serialize_uint32(body + 24, MAIN_START_FROM);
	serialize_uint32(body + 28, MAIN_RUN_FROM);

	packet_send(code, body, 32);
    main_selector = !main_selector;
}

typedef struct {
	uint8_t sector_id;
	uint8_t total_size;
} tFLASH_sectors;

#ifdef USE_STDPERIPH_DRIVER
#define ADDR_COMPRESS 0x00004000
const tFLASH_sectors sectors[] = {
		{FLASH_Sector_2, (0x00004000 / ADDR_COMPRESS)},
		{FLASH_Sector_3, (0x00008000 / ADDR_COMPRESS)},
		{FLASH_Sector_4, (0x00018000 / ADDR_COMPRESS)},
		{FLASH_Sector_5, (0x00038000 / ADDR_COMPRESS)},
		{FLASH_Sector_6, (0x00058000 / ADDR_COMPRESS)},
		{FLASH_Sector_7, (0x00078000 / ADDR_COMPRESS)},
		{FLASH_Sector_8, (0x00098000 / ADDR_COMPRESS)},
		{FLASH_Sector_9, (0x000B8000 / ADDR_COMPRESS)},
		{FLASH_Sector_10,(0x000D8000 / ADDR_COMPRESS)},
		{FLASH_Sector_11,(0x000F8000 / ADDR_COMPRESS)},
};
#endif

#ifdef STM32F745xx
#define ADDR_COMPRESS 0x00004000
#define FLASH_COMPLETE HAL_OK
const tFLASH_sectors sectors[] = {
		{FLASH_SECTOR_1, (0x00008000 / ADDR_COMPRESS)},
		{FLASH_SECTOR_2, (0x00010000 / ADDR_COMPRESS)},
		{FLASH_SECTOR_3, (0x00018000 / ADDR_COMPRESS)},
		{FLASH_SECTOR_4, (0x00038000 / ADDR_COMPRESS)},
		{FLASH_SECTOR_5, (0x00058000 / ADDR_COMPRESS)},
		{FLASH_SECTOR_6, (0x00078000 / ADDR_COMPRESS)},
		{FLASH_SECTOR_7, (0x00098000 / ADDR_COMPRESS)},
};
#endif

static void sfu_command_erase(uint8_t code, uint8_t *body, uint32_t size)
{
	if (size != 4) return;

	uint32_t firmware_size = deserialize_uint32(body);

	if (firmware_size > 0)
	{
        fw_fullbody_crc32 = UINT32_MAX;
        main_selector = !main_selector;        
        main_update_started = true;
        
        bin2page_reset();
        erase_sign_block();
        rx_dma_check();
#ifdef USING_PICO_SDK
        {
            uint32_t pos = 0;
            while (pos < firmware_size) {
                uint32_t ints = save_and_disable_interrupts();
                flash_range_erase(MAIN_START_FROM - FLASH_BASE + pos, FLASH_BLOCK_SIZE);
                restore_interrupts(ints);
                rx_dma_check();
                pos += FLASH_BLOCK_SIZE;
                //if ((pos & (FLASH_BLOCK_SIZE-1)) == 0) {
                    uint32_t i = pos / FLASH_BLOCK_SIZE;
                    packet_send(SFU_CMD_ERASE_PART, (uint8_t *)&i, sizeof(i)); 
                //}
            }
        }

        for (const uint32_t *addr = (const uint32_t *)MAIN_START_FROM; addr < (const uint32_t *)(MAIN_START_FROM + firmware_size); addr++) {
            if ((*addr) != 0xFFFFFFFF) {
                printf("Erase Error in address: 0x%08X\n", (uint32_t)addr);
                break;
            }
        }

        // for (int i = 0; i < 20; i++) {sleep_ms(200); packet_send(SFU_CMD_ERASE_PART, (uint8_t *)&i, sizeof(i)); rx_dma_check();}
        write_addr = MAIN_START_FROM;
        write_addr_real = MAIN_START_FROM;
        write_error = false;
        packet_send(code, body, size);
        return;
    }
#else
#ifdef USE_STDPERIPH_DRIVER
		FLASH_Status status = FLASH_BUSY;
		FLASH_Unlock_inline();
		FLASH_ClearFlag_inline(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
	                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
#endif

#ifdef STM32F745xx
		HAL_StatusTypeDef status = HAL_ERROR;
		HAL_FLASH_Unlock();
#endif

		for (uint32_t pos = 0; pos < LENGTH(sectors); pos++)
		{
#ifdef USE_STDPERIPH_DRIVER
			if ((FLASH_SIZE == 512) && (sectors[pos].sector_id == FLASH_Sector_7)) break;

			status = FLASH_EraseSector_inline(sectors[pos].sector_id, VoltageRange_3);
			if (status != FLASH_COMPLETE)
				break;
#endif

#ifdef STM32F745xx
			FLASH_EraseInitTypeDef erase_info = {
					.TypeErase = FLASH_TYPEERASE_SECTORS,
					.Sector = sectors[pos].sector_id,
					.NbSectors = 1,
					.VoltageRange = FLASH_VOLTAGE_RANGE_3,
			};
			uint32_t error = 0;

			status = HAL_FLASHEx_Erase(&erase_info, &error);
			if (status != HAL_OK)
				break;
#endif

			packet_send(SFU_CMD_ERASE_PART, (uint8_t *)&pos, sizeof(pos));

			uint32_t erased_size = ((uint32_t)sectors[pos].total_size) * ADDR_COMPRESS;
			if (erased_size >= firmware_size)
				break;
		}

#ifdef USE_STDPERIPH_DRIVER
		FLASH_Lock_inline();
#endif

#ifdef STM32F745xx
		HAL_FLASH_Lock();
#endif

		if (status == FLASH_COMPLETE)
		{
			write_addr = MAIN_START_FROM;
			packet_send(code, body, size);
			return;
		}
	}
#endif 

	packet_send(code, body, 0);
}

#ifdef USE_STDPERIPH_DRIVER
__attribute__ ((long_call, section(".data")))
FLASH_Status flash_block_write(uint32_t wr_addr, uint32_t *data, uint32_t count)
{
	FLASH_Status status = FLASH_BUSY;
	FLASH_Unlock_inline();

	FLASH_ClearFlag_inline(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

	while (count--)
	{
		status = FLASH_ProgramWord_inline(wr_addr, *data);
		if (status != FLASH_COMPLETE) break;

		wr_addr += 4;
		data++;
	}
	FLASH_Lock_inline();
	return status;
}
#endif

#ifdef STM32F745xx
HAL_StatusTypeDef flash_block_write(uint32_t wr_addr, uint32_t *data, uint32_t count)
{
	HAL_StatusTypeDef status = HAL_ERROR;
	HAL_FLASH_Unlock();


	while (count--)
	{
		status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, wr_addr, *data);
		if (status != FLASH_COMPLETE) break;

		wr_addr += 4;
		data++;
	}
	HAL_FLASH_Lock();
	return status;
}
#endif

void sfu_real_writer(uint8_t *block)
{
    if ((write_addr_real <= MAIN_RUN_FROM) && 
        ((write_addr_real + FLASH_PAGE_SIZE) >= (MAIN_RUN_FROM + 2*4))) {
        if (!check_run_context((uint32_t*)((uint32_t)block + (MAIN_RUN_FROM - MAIN_START_FROM) - (write_addr_real - MAIN_START_FROM))))
        {
            write_error = true;
            return;
        }
    }
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(write_addr_real - FLASH_BASE, (const void*)block, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
    rx_dma_check();
    write_addr_real += FLASH_PAGE_SIZE;
}

static void sfu_command_write(uint8_t code, uint8_t *body, uint32_t size)
{
	if (size > 4)
	{
		uint32_t body_addr = deserialize_uint32(body);

		if ((write_addr & 0xFF000000) != FLASH_BASE)
		{
			packet_send(SFU_CMD_WRERROR, body, 0);
			return;
		}

		uint32_t *word_data = (uint32_t*)&(body[4]);
		uint32_t word_count = (size - 4) / 4;

		//printf("WR:\t%08X\t%08X\t%u\n", body_addr, write_addr, word_count);		//send_str("WR\n");

		if ((body_addr == write_addr) && (word_count > 0))
		{
            fw_fullbody_crc32 = crc32_calc_raw(fw_fullbody_crc32, word_data, word_count);

#ifdef USING_PICO_SDK
            uint8_t buf[FLASH_PAGE_SIZE];
            for (uint32_t offs = 0; offs < (word_count*4); offs += sizeof(buf)) {
                memset(buf, 0xFF, sizeof(buf));
                size_t cnt = sizeof(buf);
                if (cnt + offs > (word_count*4)) {
                    cnt = (word_count*4) - offs;
                };
                memcpy(buf, (void*)((uint32_t)word_data + offs), cnt);
                
                bin2page_decode(buf, main_selector, sfu_real_writer);

                if (write_error) {
                    send_str("ERROR: Starting context wrong!\n");
                    packet_send(SFU_CMD_WRERROR, body, 0);
                    return;
                }                
            }
            // uint32_t ints = save_and_disable_interrupts();
            // flash_range_program(write_addr - FLASH_BASE, (const uint8_t *) word_data, word_count*4);
            // restore_interrupts (ints);
            // if ((word_count & 0x3F) != 0) {
            //     printf("Error: sfu_command_write ((word_count & 0x3F) != 0): addr:0x%08X, cnt:%i", write_addr, word_count);
            // }
#else 
			if (flash_block_write(write_addr, word_data, word_count) != FLASH_COMPLETE)
			{
				write_addr = 0;
				packet_send(SFU_CMD_WRERROR, body, 0);
				return;
			};
#endif

			write_addr += (word_count * 4);
		}
	}

	serialize_uint32(body + 0, write_addr);
	serialize_uint32(body + 4, receive_count());

	packet_send(code, body, 8);
}

#ifndef USING_PICO_SDK
__attribute__( ( naked ) )
void jump_main(uint32_t stack, uint32_t func)
{
	__set_MSP(stack);
	(*(void (*)())(func))();
}
#endif

bool check_first_start_once(){
    if (FIRST_START != UINT8_MAX) {
        return false;
    } 
    uint8_t write_buf[NV_PARAM_BLOCK_SIZE];
    memcpy(write_buf, NV_PARAM_BLOCK, sizeof(write_buf));
    write_buf[FIRST_START_OFFSET] = 0x00;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(NV_PARAM_BLOCK_OFFSET - FLASH_BASE, (const void*)write_buf, sizeof(write_buf));
    restore_interrupts (ints);
    send_str("check_first_start_once unset\n");
    return true;
}

void erase_sign_block() {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(MAIN_END - FLASH_BASE, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
}

void add_sign_crc() {
    uint32_t new_crc_sign = crc32_IEEE8023((const void *)MAIN_START_FROM, MAIN_END-MAIN_START_FROM);
    uint32_t new_time_stamp = max_time_stamp + 1;
    if (((*MAIN_CRC) != UINT32_MAX) || ((*MAIN_TIME_STAMP) != UINT32_MAX)) {
        send_str("erase sign block\n");
        erase_sign_block();
        send_str("erase sign block DONE\n");            
    }

    uint32_t write_buf[FLASH_PAGE_SIZE / sizeof(uint32_t)] = {[0 ... ((FLASH_PAGE_SIZE / sizeof(uint32_t))-1)] = UINT32_MAX};
    write_buf[MAIN_CRC_OFFSET] = new_crc_sign;
    write_buf[MAIN_TIME_STAMP_OFFSET] = new_time_stamp;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(MAIN_END - FLASH_BASE, (const void*)write_buf, sizeof(write_buf));
    restore_interrupts (ints);
    //printf("NEW CRC SIGN: 0x%08X\n", new_crc_sign);
    //printf("NEW TIME_VAL: 0x%08X\n", new_time_stamp);
    send_str("NEW CRC32 IEEE802.3 SIGNED\n");
    sleep_ms(2);
}

void main_start()
{
	uint32_t *boot_from = (uint32_t*)MAIN_RUN_FROM;
    if (!check_run_context(boot_from)) {
        send_str("CONTEXT ERROR\n");
        return;
    }

    //already checked in find_latest_variant
    // if (crc32_calc((const void *)MAIN_START_FROM, MAIN_END-MAIN_RUN_FROM) != (*MAIN_CRC)) {
    //     return send_str("CRC32 ERROR\n");
    // }

	send_str("CONTEXT OK\n\n");
    sleep_us(1500); //time to send "context ok" message

#ifdef USE_STDPERIPH_DRIVER
	usart_deinit();
	RCC_DeInit();
	jump_main(boot_from[0], boot_from[1]);
#endif

#ifdef STM32F745xx
	HAL_UART_DeInit(&huart1);
	HAL_CRC_DeInit(&hcrc);
	HAL_RCC_DeInit();
	HAL_DeInit();
	SysTick_Config(SysTick_LOAD_RELOAD_Msk);
	SysTick->CTRL = 0x00000000;
	jump_main(boot_from[0], boot_from[1]);
#endif

#ifdef USING_PICO_SDK
    usart_deinit();
    rp2040_quiesce_and_jump((uint32_t)boot_from);
    return;
#endif
}

static void sfu_command_start(uint8_t code, uint8_t *body, uint32_t size)
{
	if (size != 4) return;

    int bin2page_errors = bin2page_finish(sfu_real_writer);

	uint32_t *from = (uint32_t*)MAIN_START_FROM;
	uint32_t count = (write_addr - MAIN_START_FROM);
    
    uint32_t crc = fw_fullbody_crc32;
	uint32_t need = deserialize_uint32(body);

	serialize_uint32(body + 0, (uint32_t)from);
	serialize_uint32(body + 4, count);
	serialize_uint32(body + 8, crc);

	packet_send(code, body, 12);

    write_addr = 0;
    write_addr_real = 0;
    write_error = false;
    main_update_started = false;

    if (bin2page_errors != 0) {
        send_str("bin2page_errors!\n");
    } else {
        if (crc == need)
        {
            write_addr = 0;

            send('\n');
            send_str("CRC OK\n");

            add_sign_crc();
            main_start();
        }
        else
            send_str("CRC != NEED\n");
    }
}
