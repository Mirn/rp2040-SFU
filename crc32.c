#include <stdint.h>
#include <stddef.h>

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

static inline uint32_t crc32_stm32_words(const uint32_t *data, size_t word_count) {
    uint32_t crc = 0xFFFFFFFFu;                 
    for (size_t i = 0; i < word_count; ++i) {
        crc = crc32_stm_update_word_tbl(crc, data[i]);
    }
    return crc;                                  
}

uint32_t crc32_calc(const void *data, size_t len) {
    return crc32_stm32_words(data, len/4);
    // return _crc32_raw(0xFFFFFFFF, data, len);
}
