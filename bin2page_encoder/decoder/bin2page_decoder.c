#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "bin2page_decoder.h"

#define BIN2PAGE_SIGN_STR "BIN2Page"
#define BIN2PAGE_SIGN_LEN 8
#define BIN2PAGE_EXTRA_LEN 0
#define BIN2PAGE_HEADER_LEN (BIN2PAGE_SIGN_LEN + BIN2PAGE_EXTRA_LEN)

//#define LOG_NORMAL(format, ...) 
#define LOG_NORMAL(format, ...) printf(format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) printf(format, ##__VA_ARGS__)
//#define LOG_ERROR(format, ...) 

static uint8_t result_buffer[BIN2PAGE_BLOCK_SIZE];
static size_t  result_pos = 0;

static inline void result_add(uint8_t byte, tBIN2page_cb cb) {
    result_buffer[result_pos] = byte;
    result_pos += 1;
    if (result_pos >= sizeof(result_buffer)) {
        result_pos = 0;
        (*cb)(result_buffer);
    }
}

static uint8_t decode_buffer[256];
static size_t  decode_pos = 0;
static bool decode_inited = false;
static bool decode_active = false;
static int decode_errors = 0;
static int input_offset = 0;
static int output_offset = 0;

void bin2page_reset() {
    decode_pos = 0;
    result_pos = 0;
    decode_inited = false;
    decode_active = false;
    decode_errors = 0;
    input_offset = 0;
    output_offset = 0;
    memset(decode_buffer, 0, sizeof(decode_buffer));
    memset(result_buffer, 0, sizeof(result_buffer));
}

void bin2page_decode(uint8_t *bytes, bool shift, /* uint8_t primary,*/ tBIN2page_cb cb) {
    size_t len = BIN2PAGE_BLOCK_SIZE;
    if (!decode_inited) {
        decode_inited = true;
        decode_active = (strncmp((const char *)bytes, BIN2PAGE_SIGN_STR, BIN2PAGE_SIGN_LEN) == 0);
        if (decode_active) {
            len -= BIN2PAGE_HEADER_LEN;
            bytes += BIN2PAGE_HEADER_LEN;
            LOG_NORMAL("%s: OK, activated, %s\n", BIN2PAGE_SIGN_STR, (shift?"with shifting":"NO shifting"));
        } else {
            LOG_NORMAL("%s: Bypass mode\n", BIN2PAGE_SIGN_STR);
        }
    }

    if (decode_active) {
        while (len > 0) {
            uint8_t byte = *bytes;
            bytes += 1;
            len -= 1;
            decode_buffer[decode_pos] = byte;
            decode_pos += 1;
            if (decode_pos >= 256) {
                decode_pos = 0;

                bool full = (decode_buffer[0] >= 0x80);
                uint8_t cnt = decode_buffer[0] & 0x7F;
                uint8_t pos = 1;
                while ((decode_buffer[pos] == 0xFF) && (cnt > 0)) {
                    pos += 1;
                    cnt -= 1;
                }
                if (full) {
                    size_t diff = pos + cnt*1;
                    size_t offs = pos + cnt*2;
                    if (shift) {
                        size_t last_addr = offs-1;
                        for (size_t i=0; i < cnt; i++) {
                            size_t addr = offs + decode_buffer[pos + i];
                            if ((addr >= 256) || (addr <= last_addr)) {
                                decode_errors += 1;
                                LOG_ERROR("bin2page_decode: Shift addr error: %i, in_offs: %08X, out_offs: %08X\n", 
                                    addr, 
                                    input_offset + i - BIN2PAGE_BLOCK_SIZE + BIN2PAGE_HEADER_LEN, 
                                    output_offset + i);
                            } else {
                                decode_buffer[addr] = decode_buffer[diff + i];
                            }
                            last_addr = addr;
                        }
                    }
                    pos = offs;
                }  else {
                    decode_errors += 1;
                    LOG_ERROR("bin2page_decode: small format not supported\n");
                    /*size_t offs = pos + cnt;
                    if (shift) {
                        for (size_t i=0; i < cnt; i++) {
                            decode_buffer[offs + decode_buffer[pos + i]] += primary;
                        }
                    } 
                    pos = offs; */
                }
                // LOG_NORMAL("%i\t%i\t%i\t%08X\t%08X\n", full, cnt, pos, input_offset, output_offset);
                for (size_t idx = pos; idx < BIN2PAGE_BLOCK_SIZE; idx++) {
                    result_add(decode_buffer[idx], cb);
                }
                output_offset += (BIN2PAGE_BLOCK_SIZE - pos);
            }
        }
    } else {
        (*cb)(bytes);
    }
    input_offset += BIN2PAGE_BLOCK_SIZE;
}

int bin2page_finish(tBIN2page_cb cb) {
    size_t padding = 0;
    while (result_pos != 0) {
        result_add(0xFF, cb);
        padding += 1;
    }
    if (padding != 0) {
        LOG_NORMAL("bin2page_finish: %u bytes padding added\n", padding);
    }
     if (decode_errors != 0) {
        LOG_ERROR("bin2page_finish: Finished with %i errors\n", decode_errors);
    }
   return decode_errors;
}