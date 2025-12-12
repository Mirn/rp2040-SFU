#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "bin2page_decoder.h"

FILE *outFile;

void cb_test(uint8_t *block) {
        perror("Error writing to file");
    if (fwrite(block, 1, BIN2PAGE_OUTPUT_BSIZE, outFile) != BIN2PAGE_OUTPUT_BSIZE) {
        exit(-1);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <file.bin>\n", argv[0]);
        return 1;
    }

    const char *fname = argv[1];
    FILE *file = fopen(fname, "rb");
    if (file == NULL) {
        printf("Error opening file: %s", fname);
        return 1;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET); // Rewind to the beginning


    size_t real_size = (file_size + 0xFFFF) & 0xFFFF0000;
    uint8_t *buffer = (uint8_t *)malloc(real_size);
//    uint8_t *result = (char *)malloc(file_size);
//    size_t  res_size = 0;
    if (buffer == NULL) {
        perror("Error allocating memory");
        fclose(file);
        return 1;
    }
    memset(buffer, 0xFF, real_size);

    size_t bytes_read = fread(buffer, 1, file_size, file);
    if (bytes_read != file_size) {
        perror("Error reading file");
        free(buffer);
        fclose(file);
        return 1;
    }
    fclose(file);


    char outname[0x10000];

    strcpy(outname, fname);
    strcat(outname, ".bin_A");
    outFile = fopen(outname, "wb");
    if (outFile == NULL) {
        printf("Failed to open output file: %s", outname);
        return 1;
    }
    bin2page_reset();
    for (size_t i = 0; i < file_size; i+= BIN2PAGE_INPUT_BSIZE) {
        bin2page_decode(buffer+i, false, cb_test);
    }
    int err_cntA = bin2page_finish(cb_test);
    if (err_cntA != 0) {
        printf("Finished with %i errors\n", err_cntA);
    }
    fclose(outFile);

    strcpy(outname, fname);
    strcat(outname, ".bin_B");
    outFile = fopen(outname, "wb");
    if (outFile == NULL) {
        printf("Failed to open output file: %s", outname);
        return 1;
    }
    bin2page_reset();
    for (size_t i = 0; i < file_size; i+= BIN2PAGE_INPUT_BSIZE) {
        bin2page_decode(buffer+i, true, cb_test);
    }
    int err_cntB = bin2page_finish(cb_test);
    if (err_cntB != 0) {
        printf("Finished with %i errors\n", err_cntB);
    }
    fclose(outFile);

    free(buffer);
    return 0;
}
