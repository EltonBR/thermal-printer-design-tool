#define _POSIX_C_SOURCE 200809L

#include "escpos.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    const char *device;
    const char *image;
    bool cut;
    unsigned drawer_pin;
} options;

static void usage(const char *program) {
    fprintf(stderr,
        "Uso: %s [--device /dev/usb/lp3] [--image arquivo.png] "
        "[--cut] [--drawer 2|5]\n",
        program);
}

static bool parse_options(int argc, char **argv, options *result) {
    *result = (options){
        .device = "/dev/usb/lp3",
        .image = "div-como-imagem.png",
        .cut = false,
        .drawer_pin = 0
    };
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--device") && i + 1 < argc) {
            result->device = argv[++i];
        } else if (!strcmp(argv[i], "--image") && i + 1 < argc) {
            result->image = argv[++i];
        } else if (!strcmp(argv[i], "--cut")) {
            result->cut = true;
        } else if (!strcmp(argv[i], "--drawer") && i + 1 < argc) {
            result->drawer_pin = (unsigned)strtoul(argv[++i], NULL, 10);
            if (result->drawer_pin != 2 && result->drawer_pin != 5) return false;
        } else {
            return false;
        }
    }
    return true;
}

static uint8_t *read_file(const char *path, size_t *length) {
    int file = open(path, O_RDONLY);
    struct stat metadata;
    if (file < 0 || fstat(file, &metadata) < 0 || metadata.st_size <= 0) {
        if (file >= 0) close(file);
        return NULL;
    }
    uint8_t *data = malloc((size_t)metadata.st_size);
    size_t offset = 0;
    while (data && offset < (size_t)metadata.st_size) {
        ssize_t count = read(file, data + offset, (size_t)metadata.st_size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        offset += (size_t)count;
    }
    close(file);
    if (!data || offset != (size_t)metadata.st_size) {
        free(data);
        return NULL;
    }
    *length = offset;
    return data;
}

static bool separator(escpos_printer *printer, const char *title) {
    return escpos_set_align(printer, ESCPOS_ALIGN_LEFT) &&
           escpos_set_bold(printer, true) &&
           escpos_text(printer, "\n--- ") &&
           escpos_text(printer, title) &&
           escpos_text_line(printer, " ---") &&
           escpos_set_bold(printer, false);
}

static bool print_test(escpos_printer *printer, const options *settings,
                       char *error, size_t error_size) {
    if (!escpos_initialize(printer) ||
        !escpos_set_align(printer, ESCPOS_ALIGN_CENTER) ||
        !escpos_set_bold(printer, true) ||
        !escpos_set_text_size(printer, 2, 2) ||
        !escpos_text_line(printer, "ESCPOS CAPABILITIES") ||
        !escpos_set_text_size(printer, 1, 1) ||
        !escpos_set_bold(printer, false) ||
        !escpos_text_line(printer, settings->device)) return false;

    if (!separator(printer, "ALINHAMENTO") ||
        !escpos_set_align(printer, ESCPOS_ALIGN_LEFT) ||
        !escpos_text_line(printer, "Esquerda") ||
        !escpos_set_align(printer, ESCPOS_ALIGN_CENTER) ||
        !escpos_text_line(printer, "Centro") ||
        !escpos_set_align(printer, ESCPOS_ALIGN_RIGHT) ||
        !escpos_text_line(printer, "Direita")) return false;

    if (!separator(printer, "ESTILOS") ||
        !escpos_text_line(printer, "Texto normal") ||
        !escpos_set_bold(printer, true) ||
        !escpos_text_line(printer, "Texto negrito") ||
        !escpos_set_bold(printer, false) ||
        !escpos_set_underline(printer, 1) ||
        !escpos_text_line(printer, "Sublinhado 1 ponto") ||
        !escpos_set_underline(printer, 2) ||
        !escpos_text_line(printer, "Sublinhado 2 pontos") ||
        !escpos_set_underline(printer, 0) ||
        !escpos_set_invert(printer, true) ||
        !escpos_text_line(printer, "Texto invertido") ||
        !escpos_set_invert(printer, false)) return false;

    if (!separator(printer, "FONTES") ||
        !escpos_set_font(printer, 0) ||
        !escpos_text_line(printer, "Fonte A: ABC 123") ||
        !escpos_set_font(printer, 1) ||
        !escpos_text_line(printer, "Fonte B: ABC 123") ||
        !escpos_set_font(printer, 0)) return false;

    if (!separator(printer, "TAMANHOS") ||
        !escpos_set_text_size(printer, 1, 1) ||
        !escpos_text_line(printer, "1x1") ||
        !escpos_set_text_size(printer, 2, 1) ||
        !escpos_text_line(printer, "2x1") ||
        !escpos_set_text_size(printer, 1, 2) ||
        !escpos_text_line(printer, "1x2") ||
        !escpos_set_text_size(printer, 2, 2) ||
        !escpos_text_line(printer, "2x2") ||
        !escpos_set_text_size(printer, 3, 3) ||
        !escpos_text_line(printer, "3x3") ||
        !escpos_set_text_size(printer, 1, 1)) return false;

    if (!separator(printer, "CODIGOS DE BARRAS") ||
        !escpos_print_barcode(printer, ESCPOS_BARCODE_EAN13,
                              "4006381333931", 64, 3, true) ||
        !escpos_text_line(printer, "") ||
        !escpos_print_barcode(printer, ESCPOS_BARCODE_CODE39,
                              "*ESCPOS*", 64, 2, true) ||
        !escpos_text_line(printer, "") ||
        !escpos_print_barcode(printer, ESCPOS_BARCODE_CODE128,
                              "{BLIB-ESCPOS", 64, 2, true) ||
        !escpos_text_line(printer, "")) return false;

    if (!separator(printer, "QR CODE") ||
        !escpos_set_align(printer, ESCPOS_ALIGN_CENTER) ||
        !escpos_print_qr(printer, "https://example.com", 19, 4, ESCPOS_QR_EC_M) ||
        !escpos_text_line(printer, "")) return false;

    size_t image_length = 0;
    uint8_t *image = read_file(settings->image, &image_length);
    if (!image) {
        snprintf(error, error_size, "não foi possível ler %s", settings->image);
        return false;
    }
    bool image_ok = separator(printer, "IMAGEM 1-BIT") &&
        escpos_printer_print_png(printer, &(escpos_config){
            .device = settings->device,
            .max_width = 384,
            .threshold = 128,
            .image_mode = ESCPOS_IMAGE_RASTER,
            .regular_file = false
        }, image, image_length, error, error_size);
    free(image);
    if (!image_ok) return false;

    if (!separator(printer, "CONTROLES OPCIONAIS")) return false;
    if (settings->drawer_pin) {
        if (!escpos_text_line(printer, "Pulso da gaveta enviado") ||
            !escpos_cash_drawer(printer, settings->drawer_pin)) return false;
    } else if (!escpos_text_line(printer, "Gaveta: nao testada")) return false;
    if (!settings->cut && !escpos_text_line(printer, "Corte: nao testado")) return false;

    if (!escpos_feed(printer, 4)) return false;
    if (settings->cut && !escpos_cut(printer, ESCPOS_CUT_FULL, false)) return false;
    return true;
}

int main(int argc, char **argv) {
    if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        usage(argv[0]);
        return 0;
    }
    options settings;
    if (!parse_options(argc, argv, &settings)) {
        usage(argv[0]);
        return 2;
    }

    escpos_printer printer = {.fd = -1};
    char error[512] = {0};
    if (!escpos_open(&printer, settings.device, error, sizeof(error))) {
        fprintf(stderr, "Erro: %s\n", error);
        return 1;
    }
    bool success = print_test(&printer, &settings, error, sizeof(error));
    int saved_errno = errno;
    escpos_close(&printer);
    if (!success) {
        fprintf(stderr, "Erro no teste: %s%s%s\n", error,
                error[0] ? "" : strerror(saved_errno), "");
        return 1;
    }
    puts("Teste de capacidades enviado com sucesso.");
    return 0;
}
