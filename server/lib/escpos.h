#ifndef ESCPOS_H
#define ESCPOS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ESCPOS_IMAGE_RASTER,
    ESCPOS_IMAGE_BIT_IMAGE
} escpos_image_mode;

typedef struct {
    const char *device;
    unsigned max_width;
    unsigned threshold;
    escpos_image_mode image_mode;
    bool regular_file;
} escpos_config;

typedef struct {
    int fd;
} escpos_printer;

typedef enum {
    ESCPOS_ALIGN_LEFT,
    ESCPOS_ALIGN_CENTER,
    ESCPOS_ALIGN_RIGHT
} escpos_align;

typedef enum {
    ESCPOS_CUT_FULL,
    ESCPOS_CUT_PARTIAL
} escpos_cut_mode;

typedef enum {
    ESCPOS_BARCODE_UPCA,
    ESCPOS_BARCODE_UPCE,
    ESCPOS_BARCODE_EAN13,
    ESCPOS_BARCODE_EAN8,
    ESCPOS_BARCODE_CODE39,
    ESCPOS_BARCODE_ITF,
    ESCPOS_BARCODE_CODABAR,
    ESCPOS_BARCODE_CODE128
} escpos_barcode;

typedef enum {
    ESCPOS_QR_EC_L,
    ESCPOS_QR_EC_M,
    ESCPOS_QR_EC_Q,
    ESCPOS_QR_EC_H
} escpos_qr_ec;

escpos_config escpos_default_config(const char *device);

bool escpos_open(escpos_printer *printer, const char *device, char *error, size_t error_size);
void escpos_close(escpos_printer *printer);
bool escpos_raw(escpos_printer *printer, const void *data, size_t length);
bool escpos_initialize(escpos_printer *printer);
bool escpos_text(escpos_printer *printer, const char *text);
bool escpos_text_line(escpos_printer *printer, const char *text);
bool escpos_feed(escpos_printer *printer, unsigned lines);
bool escpos_set_align(escpos_printer *printer, escpos_align align);
bool escpos_set_bold(escpos_printer *printer, bool enabled);
bool escpos_set_underline(escpos_printer *printer, unsigned thickness);
bool escpos_set_invert(escpos_printer *printer, bool enabled);
bool escpos_set_font(escpos_printer *printer, unsigned font);
bool escpos_set_text_size(escpos_printer *printer, unsigned width, unsigned height);
bool escpos_cut(escpos_printer *printer, escpos_cut_mode mode, bool feed_first);
bool escpos_cash_drawer(escpos_printer *printer, unsigned pin);
bool escpos_print_barcode(escpos_printer *printer, escpos_barcode type,
                          const char *data, unsigned height, unsigned width,
                          bool text_below);
bool escpos_print_qr(escpos_printer *printer, const void *data, size_t length,
                     unsigned size, escpos_qr_ec correction);
bool escpos_printer_print_png(escpos_printer *printer, const escpos_config *config,
                              const uint8_t *png_data, size_t png_length,
                              char *error, size_t error_size);

bool escpos_print_png(const escpos_config *config,
                      const uint8_t *png_data,
                      size_t png_length,
                      char *error,
                      size_t error_size);

bool escpos_print_png_file(const escpos_config *config,
                           const char *png_path,
                           char *error,
                           size_t error_size);

#endif
