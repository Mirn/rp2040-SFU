#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

FILE *outFile;

uint8_t result_buffer[256];
size_t  result_pos = 0;

void result_add(uint8_t byte) {
    result_buffer[result_pos] = byte;
    result_pos += 1;
    if (result_pos >= sizeof(result_buffer)) {
        result_pos = 0;

        if (fwrite(result_buffer, 1, sizeof(result_buffer), outFile) != sizeof(result_buffer)) {
            perror("Error writing to file");
            exit(-1);
        }
    }
}

uint8_t decode_buffer[256];
size_t  decode_pos = 0;

void diff2040decode(uint8_t byte, bool shift, uint8_t primary) {
    decode_buffer[decode_pos] = byte;
    decode_pos += 1;
    if (decode_pos >= 256) {
        decode_pos = 0;

        bool full = decode_buffer[0] >= 0x80;
        uint8_t cnt = decode_buffer[0] & 0x7F;
        uint8_t pos = 1;
        while (decode_buffer[pos] == 0xFF) {
            pos += 1;
            cnt -= 1;
        }
        if (full) {
            size_t diff = pos + cnt*1;
            size_t offs = pos + cnt*2;
            if (shift) {
                for (size_t i=0; i < cnt; i++) {
                    decode_buffer[offs + decode_buffer[pos + i]] = decode_buffer[diff + i];
                }
            }
            pos = offs;
        } else {
            size_t offs = pos + cnt;
            if (shift) {
                for (size_t i=0; i < cnt; i++) {
                    decode_buffer[offs + decode_buffer[pos + i]] += primary;
                }
            } 
            pos = offs;
        }
        printf("%i\t%i\t%i\n", full, cnt, pos);
        for (size_t idx = pos; idx < 256; idx++) {
            result_add(decode_buffer[idx]);
        }
    }
}

int main() {
    FILE *file = fopen("Z:\\other_prj\\rp2040-SFU\\rp2040-SFU\\rp2040-SFU\\sfu_diff8bit_linker\\test.bin2040diff", "rb");
    if (file == NULL) {
        perror("Error opening file");
        return 1;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET); // Rewind to the beginning


    uint8_t *buffer = (uint8_t *)malloc(file_size);
//    uint8_t *result = (char *)malloc(file_size);
//    size_t  res_size = 0;
    if (buffer == NULL) {
        perror("Error allocating memory");
        fclose(file);
        return 1;
    }

    size_t bytes_read = fread(buffer, 1, file_size, file);
    if (bytes_read != file_size) {
        perror("Error reading file");
        free(buffer);
        fclose(file);
        return 1;
    }

    printf("First 10 bytes of the binary file:\n");
    for (int i = 0; i < 10 && i < file_size; i++) {
        printf("%02X ", (unsigned char)buffer[i]);
    }
    printf("\n");

    outFile = fopen("output.bin", "wb"); // "wb" for write binary mode

    if (outFile == NULL) {
        perror("Failed to open output file");
        return 1;
    }

    for (size_t i = 0; i < (file_size / 1); i++) {
        diff2040decode(buffer[i], false, 0x20);
    }


    fclose(outFile);
    free(buffer);
    fclose(file);

    return 0;
}