#include <stdint.h>

// NOTE:
// BIN2PAGE_INPUT_BSIZE is limited by the on-wire format:
// patch offsets are stored as uint8_t and address bytes inside a single block.
// If you need larger blocks, the BIN2Page format itself must be redesigned.

#define BIN2PAGE_OUTPUT_BSIZE 256
#define BIN2PAGE_INPUT_BSIZE  256 //WARNING: MUST BE 8 bit!

typedef void (*tBIN2page_cb)(uint8_t *block);

void bin2page_reset();
void bin2page_decode(uint8_t *bytes, bool shift, /* uint8_t primary,*/ tBIN2page_cb cb);
int  bin2page_finish(tBIN2page_cb cb);