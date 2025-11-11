void crc32_init_table(void);
uint32_t _crc32_raw(uint32_t crc, const void *data, size_t len);
uint32_t crc32_calc(const void *data, size_t len);
