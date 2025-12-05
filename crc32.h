#include <stdint.h>
#include <stdlib.h>
void crc32_init_table(void);
uint32_t crc32_calc_raw(uint32_t crc, const uint32_t *data, size_t word_count);
uint32_t crc32_calc(const void *data, size_t len);

//for CRC-32-IEEE 802.3 support
void crc32_IEEE8023_init(void);
uint32_t crc32_IEEE8023(const void *data, size_t len);
