#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "crc32.h"

// static uint32_t crc32_table[256];
// static int crc32_table_ready = 0;

// void crc32_init_table(void) {
//     if (crc32_table_ready) return;
//     for (uint32_t i = 0; i < 256; i++) {
//         uint32_t c = i;
//         for (int k = 0; k < 8; k++) {
//             c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
//         }
//         crc32_table[i] = c;
//     }
//     crc32_table_ready = 1;
// }

// uint32_t _crc32_raw(uint32_t crc, const void *data, size_t len) {
//     const uint8_t *p = (const uint8_t *)data;
//     crc = ~crc;
//     for (size_t i = 0; i < len; i++) {
//         crc = crc32_table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
//     }
//     return ~crc;
// }

// static inline uint32_t crc32_stm32_update_word(uint32_t crc, uint32_t data_word) {
//     crc ^= data_word;
//     for (int i = 0; i < 32; i++) {
//         if (crc & 0x80000000u)
//             crc = (crc << 1) ^ 0x04C11DB7u;
//         else
//             crc <<= 1;
//     }
//     return crc;
// }

// #include <stdio.h>

// uint32_t crc32_stm32_words(const uint32_t *data, size_t word_count) {
//     //printf("CRC: \n");
//     uint32_t crc = 0xFFFFFFFFu;
//     for (size_t i = 0; i < word_count; i++) {
//         //printf("0x%08X ", data[i]);
//         crc = crc32_stm32_update_word(crc, data[i]);
//     }
//     //printf("\n");        
//     return crc;
// }

static uint32_t crc32_stm_tab[256];

void crc32_init_table(void) {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i << 24;              // MSB-first
        for (int k = 0; k < 8; ++k) {
            c = (c & 0x80000000u) ? ((c << 1) ^ 0x04C11DB7u) : (c << 1);
        }
        crc32_stm_tab[i] = c;
    }
}

static inline uint32_t crc32_stm_update_word_tbl(uint32_t crc, uint32_t w) {
    uint8_t b = (uint8_t)(w >> 24);
    crc = (crc << 8) ^ crc32_stm_tab[((crc >> 24) ^ b) & 0xFFu];

    b = (uint8_t)(w >> 16);
    crc = (crc << 8) ^ crc32_stm_tab[((crc >> 24) ^ b) & 0xFFu];

    b = (uint8_t)(w >> 8);
    crc = (crc << 8) ^ crc32_stm_tab[((crc >> 24) ^ b) & 0xFFu];

    b = (uint8_t)(w);
    crc = (crc << 8) ^ crc32_stm_tab[((crc >> 24) ^ b) & 0xFFu];

    return crc;
}

uint32_t crc32_calc_raw(uint32_t crc, const uint32_t *data, size_t word_count) {
    for (size_t i = 0; i < word_count; ++i) {
        crc = crc32_stm_update_word_tbl(crc, data[i]);
    }
    return crc;                                  
}

uint32_t crc32_calc(const void *data, size_t len) {
    return crc32_calc_raw(UINT32_MAX, data, len/4);
}

//#define crc32_IEEE8023_lut crc32_stm_tab
static uint32_t crc32_IEEE8023_lut[256];

void crc32_IEEE8023_init(void) {
    uint32_t const poly = 0xEDB88320; //CRC-32-IEEE 802.3
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            c = (c & 1) ? (poly ^ (c >> 1)) : (c >> 1);
        }
        crc32_IEEE8023_lut[i] = c;
    }

    if ((crc32_IEEE8023("12345678", 8) != 0x9ae0daaf) || 
        (crc32_IEEE8023("TESTABCD", 8) != 0x1fa79460)) {
        printf("crc32_IEEE8023 self-check error!\r\n");
        while (1) {
        };
    };
}

uint32_t crc32_IEEE8023_raw(uint32_t crc, const void *data, size_t len_words) {
    const uint32_t *p = (const uint32_t *)data;
    crc = ~crc;

    while (len_words--) {
        uint32_t w = *p++;
        crc = crc32_IEEE8023_lut[(crc ^ ( w        & 0xFF)) & 0xFF] ^ (crc >> 8);
        crc = crc32_IEEE8023_lut[(crc ^ ((w >>  8) & 0xFF)) & 0xFF] ^ (crc >> 8);
        crc = crc32_IEEE8023_lut[(crc ^ ((w >> 16) & 0xFF)) & 0xFF] ^ (crc >> 8);
        crc = crc32_IEEE8023_lut[(crc ^ ((w >> 24) & 0xFF)) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

uint32_t crc32_IEEE8023(const void *data, size_t len) {
    return crc32_IEEE8023_raw(0, data, len/4);
}
  
    // volatile uint32_t t1 = time_us_32();
    // uint32_t crc = crc32_calc((const void *)0x10010000, 2000000);
    // volatile uint32_t t2 = time_us_32();
    // printf("CRC32=0x%08X\t%i\n\n", crc, t2-t1);

    // t1 = time_us_32();
    // crc = crc32_IEEE8023((const void *)0x10010000, 2000000);
    // t2 = time_us_32();
    // printf("crc32_IEEE8023 = 0x%08X\t%i\n", crc, t2-t1);

    // t1 = time_us_32();
    // crc = crc32_IEEE8023_words((const void *)0x10010000, 2000000 / 4);
    // t2 = time_us_32();
    // printf("IEEE8023_words = 0x%08X\t%i\n", crc, t2-t1);

    // t1 = time_us_32();
    // crc = crc32_IEEE8023_words_simple((const void *)0x10010000, 2000000 / 4);
    // t2 = time_us_32();
    // printf("words_simple  = 0x%08X\t%i\n", crc, t2-t1);
