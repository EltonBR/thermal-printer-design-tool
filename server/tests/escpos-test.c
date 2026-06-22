#include "escpos.h"

#include <fcntl.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int check_mode(const uint8_t *png, size_t png_length, escpos_image_mode mode,
                      const uint8_t *expected, size_t expected_length) {
    const char *path = "/tmp/libescpos-test.bin";
    escpos_config config = escpos_default_config(path);
    config.regular_file = true;
    config.image_mode = mode;
    config.max_width = 1;
    char error[256];
    if (!escpos_print_png(&config, png, png_length, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    FILE *file = fopen(path, "rb");
    uint8_t actual[64];
    size_t actual_length = file ? fread(actual, 1, sizeof(actual), file) : 0;
    if (file) fclose(file);
    if (actual_length != expected_length || memcmp(actual, expected, expected_length)) {
        fprintf(stderr, "saída ESC/POS diferente da referência\n");
        return 1;
    }
    return 0;
}

static int check_dithering(void) {
    png_image image = {0};
    image.version = PNG_IMAGE_VERSION;
    image.width = 16;
    image.height = 16;
    image.format = PNG_FORMAT_RGBA;
    uint8_t gray[16U * 16U * 4U];
    for (size_t i = 0; i < sizeof(gray); i += 4) {
        gray[i] = gray[i + 1] = gray[i + 2] = 128;
        gray[i + 3] = 255;
    }
    size_t png_length = 0;
    png_image_write_to_memory(&image, NULL, &png_length, 0, gray, 0, NULL);
    uint8_t *png = malloc(png_length);
    if (!png || !png_image_write_to_memory(&image, png, &png_length, 0, gray, 0, NULL)) {
        free(png);
        return 1;
    }
    const char *path = "/tmp/libescpos-dither-test.bin";
    escpos_config config = escpos_default_config(path);
    config.regular_file = true;
    config.max_width = 16;
    char error[256];
    if (!escpos_print_png(&config, png, png_length, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        free(png);
        return 1;
    }
    free(png);
    FILE *file = fopen(path, "rb");
    uint8_t output[64];
    size_t length = file ? fread(output, 1, sizeof(output), file) : 0;
    if (file) fclose(file);
    bool has_black = false;
    bool has_white = false;
    for (size_t i = 8; i < length; i++) {
        if (output[i] != 0x00) has_black = true;
        if (output[i] != 0xff) has_white = true;
    }
    if (!has_black || !has_white) {
        fprintf(stderr, "dithering não preservou o tom intermediário\n");
        return 1;
    }
    return 0;
}

static int check_command_api(void) {
    const char *path = "/tmp/libescpos-command-test.bin";
    escpos_printer printer = {.fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644)};
    if (printer.fd < 0) return 1;
    const char *ean13 = "4006381333931";
    const char *qr = "1234";
    bool success =
        escpos_initialize(&printer) &&
        escpos_text_line(&printer, "Oi") &&
        escpos_set_align(&printer, ESCPOS_ALIGN_CENTER) &&
        escpos_set_bold(&printer, true) &&
        escpos_set_underline(&printer, 2) &&
        escpos_set_invert(&printer, true) &&
        escpos_set_font(&printer, 1) &&
        escpos_set_text_size(&printer, 2, 3) &&
        escpos_feed(&printer, 3) &&
        escpos_cash_drawer(&printer, 2) &&
        escpos_cut(&printer, ESCPOS_CUT_FULL, false) &&
        escpos_print_barcode(&printer, ESCPOS_BARCODE_EAN13, ean13, 64, 3, true) &&
        escpos_print_qr(&printer, qr, strlen(qr), 3, ESCPOS_QR_EC_L);
    escpos_close(&printer);
    if (!success) return 1;

    const uint8_t expected[] = {
        0x1b, 0x40, 'O', 'i', '\n',
        0x1b, 0x61, 0x01,
        0x1b, 0x45, 0x01,
        0x1b, 0x2d, 0x02,
        0x1d, 0x42, 0x01,
        0x1b, 0x4d, 0x01,
        0x1d, 0x21, 0x12,
        0x1b, 0x64, 0x03,
        0x1b, 0x70, 0x00, 0x32, 0x32,
        0x1d, 0x56, 0x42, 0x00,
        0x1b, 0x61, 0x01,
        0x1d, 0x68, 0x40,
        0x1d, 0x77, 0x03,
        0x1d, 0x66, 0x00,
        0x1d, 0x48, 0x02,
        0x1d, 0x6b, 0x02,
        '4','0','0','6','3','8','1','3','3','3','9','3','1',0x00,
        0x1d,0x28,0x6b,0x04,0x00,0x31,0x41,0x32,0x00,
        0x1d,0x28,0x6b,0x03,0x00,0x31,0x43,0x03,
        0x1d,0x28,0x6b,0x03,0x00,0x31,0x45,0x30,
        0x1d,0x28,0x6b,0x07,0x00,0x31,0x50,0x30,'1','2','3','4',
        0x1d,0x28,0x6b,0x03,0x00,0x31,0x51,0x30
    };
    FILE *file = fopen(path, "rb");
    uint8_t actual[256];
    size_t length = file ? fread(actual, 1, sizeof(actual), file) : 0;
    if (file) fclose(file);
    if (length != sizeof(expected) || memcmp(actual, expected, sizeof(expected))) {
        fprintf(stderr, "API de comandos divergiu dos vetores python-escpos\n");
        return 1;
    }
    return 0;
}

int main(void) {
    png_image image = {0};
    image.version = PNG_IMAGE_VERSION;
    image.width = 1;
    image.height = 1;
    image.format = PNG_FORMAT_RGBA;
    const uint8_t black[] = {0, 0, 0, 255};
    size_t png_length = 0;
    if (!png_image_write_to_memory(&image, NULL, &png_length, 0, black, 0, NULL)) return 1;
    uint8_t *png = malloc(png_length);
    if (!png || !png_image_write_to_memory(&image, png, &png_length, 0, black, 0, NULL)) {
        free(png);
        return 1;
    }

    const uint8_t raster[] = {0x1d, 0x76, 0x30, 0x00, 0x01, 0x00, 0x01, 0x00, 0x80};
    const uint8_t column[] = {
        0x1b, 0x33, 0x10, 0x1b, 0x2a, 0x21, 0x01, 0x00,
        0x80, 0x00, 0x00, 0x0a, 0x1b, 0x32
    };
    int result = check_mode(png, png_length, ESCPOS_IMAGE_RASTER, raster, sizeof(raster)) |
                 check_mode(png, png_length, ESCPOS_IMAGE_BIT_IMAGE, column, sizeof(column)) |
                 check_dithering() |
                 check_command_api();
    free(png);
    if (result == 0) puts("Formatos ESC/POS correspondem aos vetores do python-escpos.");
    return result;
}
