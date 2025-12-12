#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "bin2page_decoder.h"

FILE *outFile;

void cb_test(uint8_t *block) {
    if (fwrite(block, 1, BIN2PAGE_OUTPUT_BSIZE, outFile) != BIN2PAGE_OUTPUT_BSIZE) {
        fprintf(stderr, "Error writing to file\n");
        exit(1); //TODO - FIX IT IF REMAKE ==========================================================================================
    }
}

int main(int argc, char *argv[]) {
    int retval = 1;
    if (argc != 2) {
        printf("Usage: %s <file.bin>\n", argv[0]);
        return 1;
    }

    const char *fname = argv[1];
    FILE *file = fopen(fname, "rb");
    if (file == NULL) {
        fprintf(stderr, "Error opening file: %s\n", fname);
        return 1;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET); // Rewind to the beginning

    if (file_size <= 0) {
        fprintf(stderr, "Input file is empty or ftell() failed\n");
        fclose(file);
        return 1;
    }

    size_t real_size = (file_size + 0xFFFF) & 0xFFFF0000;
    uint8_t *buffer = (uint8_t *)malloc(real_size);
//    uint8_t *result = (char *)malloc(file_size);
//    size_t  res_size = 0;
    if (buffer == NULL) {
        fprintf(stderr, "Error allocating memory\n");
        fclose(file);
        return 1;
    }
    memset(buffer, 0xFF, real_size);

    size_t bytes_read = fread(buffer, 1, file_size, file);
    if (bytes_read != file_size) {
        fprintf(stderr, "Error reading file\n");
        free(buffer);
        fclose(file);
        return 1;
    }
    fclose(file);


    retval = 0;
    char outname[0x10000];

    strcpy(outname, fname);
    strcat(outname, ".bin_A");
    outFile = fopen(outname, "wb");
    if (outFile == NULL) {
        fprintf(stderr, "Failed to open output file: %s\n", outname);
        free(buffer);
        return 1;
    }
    bin2page_reset();
    for (size_t i = 0; i < file_size; i+= BIN2PAGE_INPUT_BSIZE) {
        bin2page_decode(buffer+i, false, cb_test);
    }
    int err_cntA = bin2page_finish(cb_test);
    if (err_cntA != 0) {
        fprintf(stderr, "Finished with %i errors\n", err_cntA);
        printf("Finished with %i errors\n", err_cntA);
        retval |= 2;
    }
    fclose(outFile);

    strcpy(outname, fname);
    strcat(outname, ".bin_B");
    outFile = fopen(outname, "wb");
    if (outFile == NULL) {
        fprintf(stderr, "Failed to open output file: %s\n", outname);
        free(buffer);
        return 1;
    }
    bin2page_reset();
    for (size_t i = 0; i < file_size; i+= BIN2PAGE_INPUT_BSIZE) {
        bin2page_decode(buffer+i, true, cb_test);
    }
    int err_cntB = bin2page_finish(cb_test);
    if (err_cntB != 0) {
        fprintf(stderr, "Finished with %i errors\n", err_cntB);
        printf("Finished with %i errors\n", err_cntB);
        retval |= 4;
    }
    fclose(outFile);

    free(buffer);
    return retval;
}
