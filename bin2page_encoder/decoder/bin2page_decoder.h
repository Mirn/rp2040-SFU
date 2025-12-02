#include <stdint.h>

#define BIN2PAGE_BLOCK_SIZE 256
typedef void (*tBIN2page_cb)(uint8_t *block);

void bin2page_reset();
void bin2page_decode(uint8_t *bytes, bool shift, /* uint8_t primary,*/ tBIN2page_cb cb);
int  bin2page_finish(tBIN2page_cb cb);