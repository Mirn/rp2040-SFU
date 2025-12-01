void crc32_init_table(void);
uint32_t crc32_calc_raw(uint32_t crc, const uint32_t *data, size_t word_count);
uint32_t crc32_calc(const void *data, size_t len);
