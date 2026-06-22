#define _POSIX_C_SOURCE 200809L

#include "escpos.h"

#include <errno.h>
#include <fcntl.h>
#include <png.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool write_all(int fd, const uint8_t *data, size_t length) {
    while (length > 0) {
        ssize_t count = write(fd, data, length);
        if (count > 0) {
            data += count;
            length -= (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd descriptor = {.fd = fd, .events = POLLOUT};
            int ready;
            do {
                ready = poll(&descriptor, 1, 3000);
            } while (ready < 0 && errno == EINTR);
            if (ready > 0) continue;
            if (ready == 0) errno = ETIMEDOUT;
        }
        return false;
    }
    return true;
}

static unsigned pixel_luminance(const uint8_t *pixel) {
    unsigned alpha = pixel[3];
    unsigned red = (pixel[0] * alpha + 255U * (255U - alpha)) / 255U;
    unsigned green = (pixel[1] * alpha + 255U * (255U - alpha)) / 255U;
    unsigned blue = (pixel[2] * alpha + 255U * (255U - alpha)) / 255U;
    return (299U * red + 587U * green + 114U * blue) / 1000U;
}

typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t *pixels;
} monochrome_image;

static bool make_monochrome(const escpos_config *config, const png_image *image,
                            const uint8_t *rgba, monochrome_image *result) {
    result->width = image->width > config->max_width ? config->max_width : image->width;
    result->height = (uint32_t)(((uint64_t)image->height * result->width) / image->width);
    if (result->height == 0) result->height = 1;
    result->pixels = calloc((size_t)result->width * result->height, 1);
    int *current_error = calloc((size_t)result->width + 2U, sizeof(int));
    int *next_error = calloc((size_t)result->width + 2U, sizeof(int));
    if (!result->pixels || !current_error || !next_error) {
        free(result->pixels);
        free(current_error);
        free(next_error);
        result->pixels = NULL;
        return false;
    }

    for (uint32_t y = 0; y < result->height; y++) {
        uint32_t source_y = (uint32_t)(((uint64_t)y * image->height) / result->height);
        for (uint32_t x = 0; x < result->width; x++) {
            uint32_t source_x = (uint32_t)(((uint64_t)x * image->width) / result->width);
            const uint8_t *pixel = rgba + ((size_t)source_y * image->width + source_x) * 4U;
            int value = (int)pixel_luminance(pixel) + current_error[x + 1U] / 16;
            if (value < 0) value = 0;
            if (value > 255) value = 255;
            int quantized = value < (int)config->threshold ? 0 : 255;
            result->pixels[(size_t)y * result->width + x] = quantized == 0;
            int error = value - quantized;
            current_error[x + 2U] += error * 7;
            next_error[x] += error * 3;
            next_error[x + 1U] += error * 5;
            next_error[x + 2U] += error;
        }
        int *swap = current_error;
        current_error = next_error;
        next_error = swap;
        memset(next_error, 0, ((size_t)result->width + 2U) * sizeof(int));
    }
    free(current_error);
    free(next_error);
    return true;
}

static bool decode_png(const uint8_t *data, size_t length, png_image *image,
                       uint8_t **rgba, char *error, size_t error_size) {
    memset(image, 0, sizeof(*image));
    image->version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(image, data, length)) {
        snprintf(error, error_size, "PNG inválido: %s", image->message);
        return false;
    }
    image->format = PNG_FORMAT_RGBA;
    *rgba = malloc(PNG_IMAGE_SIZE(*image));
    if (!*rgba || !png_image_finish_read(image, NULL, *rgba, 0, NULL)) {
        snprintf(error, error_size, "falha ao decodificar PNG: %s", image->message);
        free(*rgba);
        *rgba = NULL;
        png_image_free(image);
        return false;
    }
    return true;
}

static uint8_t *make_raster(const monochrome_image *image, size_t *length) {
    uint32_t width = image->width;
    uint32_t height = image->height;
    size_t row_bytes = (width + 7U) / 8U;
    size_t raster_size = row_bytes * height;
    *length = 8U + raster_size;
    uint8_t *output = calloc(1, *length);
    if (!output) return NULL;

    size_t offset = 0;
    const uint8_t start[] = {0x1d, 0x76, 0x30, 0x00,
                             (uint8_t)(row_bytes & 0xffU),
                             (uint8_t)((row_bytes >> 8U) & 0xffU),
                             (uint8_t)(height & 0xffU),
                             (uint8_t)((height >> 8U) & 0xffU)};
    memcpy(output, start, sizeof(start));
    offset = sizeof(start);
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            if (image->pixels[(size_t)y * width + x]) {
                output[offset + (size_t)y * row_bytes + x / 8U] |=
                    (uint8_t)(0x80U >> (x % 8U));
            }
        }
    }
    offset += raster_size;
    return output;
}

static uint8_t *make_bit_image(const monochrome_image *image, size_t *length) {
    uint32_t width = image->width;
    uint32_t height = image->height;
    size_t strips = (height + 23U) / 24U;
    *length = 3U + strips * (5U + (size_t)width * 3U + 1U) + 2U;
    uint8_t *output = calloc(1, *length);
    if (!output) return NULL;

    size_t offset = 0;
    const uint8_t start[] = {0x1b, 0x33, 0x10};
    memcpy(output, start, sizeof(start));
    offset = sizeof(start);
    for (size_t strip = 0; strip < strips; strip++) {
        output[offset++] = 0x1b;
        output[offset++] = 0x2a;
        output[offset++] = 33;
        output[offset++] = (uint8_t)(width & 0xffU);
        output[offset++] = (uint8_t)((width >> 8U) & 0xffU);
        for (uint32_t x = 0; x < width; x++) {
            for (unsigned byte_index = 0; byte_index < 3; byte_index++) {
                uint8_t column = 0;
                for (unsigned bit = 0; bit < 8; bit++) {
                    uint32_t y = (uint32_t)(strip * 24U + byte_index * 8U + bit);
                    if (y >= height) continue;
                    if (image->pixels[(size_t)y * width + x]) {
                        column |= (uint8_t)(0x80U >> bit);
                    }
                }
                output[offset++] = column;
            }
        }
        output[offset++] = '\n';
    }
    output[offset++] = 0x1b;
    output[offset++] = 0x32;
    return output;
}

escpos_config escpos_default_config(const char *device) {
    return (escpos_config){
        .device = device,
        .max_width = 384,
        .threshold = 128,
        .image_mode = ESCPOS_IMAGE_RASTER,
        .regular_file = false
    };
}

bool escpos_open(escpos_printer *printer, const char *device, char *error, size_t error_size) {
    if (!printer || !device) {
        errno = EINVAL;
        return false;
    }
    printer->fd = open(device, O_WRONLY);
    if (printer->fd < 0) {
        snprintf(error, error_size, "não foi possível abrir %s: %s", device, strerror(errno));
        return false;
    }
    return true;
}

void escpos_close(escpos_printer *printer) {
    if (printer && printer->fd >= 0) {
        close(printer->fd);
        printer->fd = -1;
    }
}

bool escpos_raw(escpos_printer *printer, const void *data, size_t length) {
    if (!printer || printer->fd < 0 || (!data && length > 0)) {
        errno = EINVAL;
        return false;
    }
    return write_all(printer->fd, data, length);
}

static bool send_bytes(escpos_printer *printer, const uint8_t *data, size_t length) {
    return escpos_raw(printer, data, length);
}

bool escpos_initialize(escpos_printer *printer) {
    const uint8_t command[] = {0x1b, 0x40};
    return send_bytes(printer, command, sizeof(command));
}

bool escpos_text(escpos_printer *printer, const char *text) {
    if (!text) {
        errno = EINVAL;
        return false;
    }
    return escpos_raw(printer, text, strlen(text));
}

bool escpos_text_line(escpos_printer *printer, const char *text) {
    return escpos_text(printer, text) && escpos_text(printer, "\n");
}

bool escpos_feed(escpos_printer *printer, unsigned lines) {
    if (lines > 255) {
        errno = EINVAL;
        return false;
    }
    const uint8_t command[] = {0x1b, 0x64, (uint8_t)lines};
    return send_bytes(printer, command, sizeof(command));
}

bool escpos_set_align(escpos_printer *printer, escpos_align align) {
    if (align < ESCPOS_ALIGN_LEFT || align > ESCPOS_ALIGN_RIGHT) {
        errno = EINVAL;
        return false;
    }
    const uint8_t command[] = {0x1b, 0x61, (uint8_t)align};
    return send_bytes(printer, command, sizeof(command));
}

bool escpos_set_bold(escpos_printer *printer, bool enabled) {
    const uint8_t command[] = {0x1b, 0x45, enabled ? 1 : 0};
    return send_bytes(printer, command, sizeof(command));
}

bool escpos_set_underline(escpos_printer *printer, unsigned thickness) {
    if (thickness > 2) {
        errno = EINVAL;
        return false;
    }
    const uint8_t command[] = {0x1b, 0x2d, (uint8_t)thickness};
    return send_bytes(printer, command, sizeof(command));
}

bool escpos_set_invert(escpos_printer *printer, bool enabled) {
    const uint8_t command[] = {0x1d, 0x42, enabled ? 1 : 0};
    return send_bytes(printer, command, sizeof(command));
}

bool escpos_set_font(escpos_printer *printer, unsigned font) {
    if (font > 1) {
        errno = EINVAL;
        return false;
    }
    const uint8_t command[] = {0x1b, 0x4d, (uint8_t)font};
    return send_bytes(printer, command, sizeof(command));
}

bool escpos_set_text_size(escpos_printer *printer, unsigned width, unsigned height) {
    if (width < 1 || width > 8 || height < 1 || height > 8) {
        errno = EINVAL;
        return false;
    }
    const uint8_t command[] = {
        0x1d, 0x21, (uint8_t)(((width - 1U) << 4U) | (height - 1U))
    };
    return send_bytes(printer, command, sizeof(command));
}

bool escpos_cut(escpos_printer *printer, escpos_cut_mode mode, bool feed_first) {
    if (mode != ESCPOS_CUT_FULL && mode != ESCPOS_CUT_PARTIAL) {
        errno = EINVAL;
        return false;
    }
    if (!feed_first) {
        const uint8_t command[] = {0x1d, 0x56, 66, 0};
        return send_bytes(printer, command, sizeof(command));
    }
    const uint8_t cut[] = {0x1d, 0x56, mode == ESCPOS_CUT_PARTIAL ? 1 : 0};
    return escpos_feed(printer, 6) && send_bytes(printer, cut, sizeof(cut));
}

bool escpos_cash_drawer(escpos_printer *printer, unsigned pin) {
    if (pin != 2 && pin != 5) {
        errno = EINVAL;
        return false;
    }
    const uint8_t command[] = {0x1b, 0x70, pin == 2 ? 0 : 1, 50, 50};
    return send_bytes(printer, command, sizeof(command));
}

bool escpos_print_barcode(escpos_printer *printer, escpos_barcode type,
                          const char *data, unsigned height, unsigned width,
                          bool text_below) {
    static const uint8_t type_a[] = {0, 1, 2, 3, 4, 5, 6};
    size_t data_length = data ? strlen(data) : 0;
    if (!data || data_length == 0 || data_length > 255 || height < 1 || height > 255 ||
        width < 2 || width > 6 || type < ESCPOS_BARCODE_UPCA || type > ESCPOS_BARCODE_CODE128) {
        errno = EINVAL;
        return false;
    }
    const uint8_t setup[] = {
        0x1b, 0x61, 0x01,
        0x1d, 0x68, (uint8_t)height,
        0x1d, 0x77, (uint8_t)width,
        0x1d, 0x66, 0x00,
        0x1d, 0x48, text_below ? 2 : 0
    };
    if (!send_bytes(printer, setup, sizeof(setup))) return false;
    if (type == ESCPOS_BARCODE_CODE128) {
        const uint8_t header[] = {0x1d, 0x6b, 73, (uint8_t)data_length};
        return send_bytes(printer, header, sizeof(header)) && escpos_raw(printer, data, data_length);
    }
    const uint8_t header[] = {0x1d, 0x6b, type_a[type]};
    const uint8_t nul = 0;
    return send_bytes(printer, header, sizeof(header)) && escpos_raw(printer, data, data_length) &&
           send_bytes(printer, &nul, 1);
}

static bool send_2d(escpos_printer *printer, uint8_t function, const uint8_t *data,
                    size_t data_length, bool modifier) {
    size_t payload_length = data_length + (modifier ? 1U : 0U) + 2U;
    if (payload_length > 65535U) {
        errno = EINVAL;
        return false;
    }
    uint8_t header[] = {0x1d, 0x28, 0x6b,
                        (uint8_t)(payload_length & 0xffU),
                        (uint8_t)((payload_length >> 8U) & 0xffU),
                        0x31, function};
    if (!send_bytes(printer, header, sizeof(header))) return false;
    if (modifier) {
        const uint8_t zero = 0x30;
        if (!send_bytes(printer, &zero, 1)) return false;
    }
    return data_length == 0 || send_bytes(printer, data, data_length);
}

bool escpos_print_qr(escpos_printer *printer, const void *data, size_t length,
                     unsigned size, escpos_qr_ec correction) {
    if ((!data && length > 0) || length > 65530U || size < 1 || size > 16 ||
        correction < ESCPOS_QR_EC_L || correction > ESCPOS_QR_EC_H) {
        errno = EINVAL;
        return false;
    }
    if (length == 0) return true;
    const uint8_t model[] = {0x32, 0x00};
    const uint8_t module_size = (uint8_t)size;
    const uint8_t ec = (uint8_t)(0x30 + correction);
    return send_2d(printer, 0x41, model, sizeof(model), false) &&
           send_2d(printer, 0x43, &module_size, 1, false) &&
           send_2d(printer, 0x45, &ec, 1, false) &&
           send_2d(printer, 0x50, data, length, true) &&
           send_2d(printer, 0x51, NULL, 0, true);
}

static uint8_t *encode_png(const escpos_config *config, const uint8_t *png_data,
                           size_t png_length, size_t *command_length,
                           char *error, size_t error_size) {
    if (!config || !png_data || png_length < 8 ||
        memcmp(png_data, "\x89PNG\r\n\x1a\n", 8)) {
        snprintf(error, error_size, "entrada não é um PNG válido");
        return NULL;
    }
    png_image image;
    uint8_t *rgba = NULL;
    if (!decode_png(png_data, png_length, &image, &rgba, error, error_size)) return NULL;

    monochrome_image monochrome = {0};
    if (!make_monochrome(config, &image, rgba, &monochrome)) {
        free(rgba);
        png_image_free(&image);
        snprintf(error, error_size, "memória insuficiente");
        return NULL;
    }
    uint8_t *command = config->image_mode == ESCPOS_IMAGE_BIT_IMAGE
        ? make_bit_image(&monochrome, command_length)
        : make_raster(&monochrome, command_length);
    free(monochrome.pixels);
    free(rgba);
    png_image_free(&image);
    if (!command) {
        snprintf(error, error_size, "memória insuficiente");
        return NULL;
    }
    return command;
}

bool escpos_printer_print_png(escpos_printer *printer, const escpos_config *config,
                              const uint8_t *png_data, size_t png_length,
                              char *error, size_t error_size) {
    size_t command_length = 0;
    uint8_t *command = encode_png(config, png_data, png_length, &command_length,
                                  error, error_size);
    if (!command) return false;
    bool success = escpos_raw(printer, command, command_length);
    if (!success) {
        snprintf(error, error_size, "falha ao escrever na impressora: %s", strerror(errno));
    }
    free(command);
    return success;
}

bool escpos_print_png(const escpos_config *config, const uint8_t *png_data,
                      size_t png_length, char *error, size_t error_size) {
    if (!config || !config->device) {
        snprintf(error, error_size, "dispositivo ESC/POS não configurado");
        errno = EINVAL;
        return false;
    }
    size_t command_length = 0;
    uint8_t *command = encode_png(config, png_data, png_length, &command_length,
                                  error, error_size);
    if (!command) return false;

    int flags = O_WRONLY | (config->regular_file ? O_CREAT | O_TRUNC : 0);
    int printer = open(config->device, flags, 0644);
    if (printer < 0) {
        snprintf(error, error_size, "não foi possível abrir %s: %s", config->device, strerror(errno));
        free(command);
        return false;
    }
    bool success = write_all(printer, command, command_length);
    int saved_errno = errno;
    close(printer);
    free(command);
    if (!success) {
        snprintf(error, error_size, "falha ao escrever em %s: %s", config->device,
                 strerror(saved_errno));
    }
    return success;
}

bool escpos_print_png_file(const escpos_config *config, const char *png_path,
                           char *error, size_t error_size) {
    int file = open(png_path, O_RDONLY);
    struct stat metadata;
    if (file < 0 || fstat(file, &metadata) < 0 || metadata.st_size <= 0) {
        if (file >= 0) close(file);
        snprintf(error, error_size, "não foi possível abrir %s: %s", png_path, strerror(errno));
        return false;
    }
    uint8_t *data = malloc((size_t)metadata.st_size);
    size_t offset = 0;
    while (data && offset < (size_t)metadata.st_size) {
        ssize_t count = read(file, data + offset, (size_t)metadata.st_size - offset);
        if (count <= 0) break;
        offset += (size_t)count;
    }
    close(file);
    if (!data || offset != (size_t)metadata.st_size) {
        free(data);
        snprintf(error, error_size, "falha ao ler %s", png_path);
        return false;
    }
    bool success = escpos_print_png(config, data, offset, error, error_size);
    free(data);
    return success;
}
