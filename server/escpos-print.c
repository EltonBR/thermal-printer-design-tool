#include "escpos.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program) {
    fprintf(stderr, "Uso: %s imagem.png [--device /dev/usb/lp3] "
                    "[--mode raster|bit-image] [--width pontos]\n", program);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }
    const char *image = argv[1];
    const char *device = "/dev/usb/lp3";
    escpos_image_mode mode = ESCPOS_IMAGE_RASTER;
    unsigned width = 384;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--device") && i + 1 < argc) {
            device = argv[++i];
        } else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
            const char *value = argv[++i];
            if (!strcmp(value, "raster")) mode = ESCPOS_IMAGE_RASTER;
            else if (!strcmp(value, "bit-image")) mode = ESCPOS_IMAGE_BIT_IMAGE;
            else {
                usage(argv[0]);
                return 2;
            }
        } else if (!strcmp(argv[i], "--width") && i + 1 < argc) {
            width = (unsigned)strtoul(argv[++i], NULL, 10);
            if (width == 0 || width > 576) {
                usage(argv[0]);
                return 2;
            }
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    escpos_config config = escpos_default_config(device);
    config.image_mode = mode;
    config.max_width = width;
    char error[512];
    if (!escpos_print_png_file(&config, image, error, sizeof(error))) {
        fprintf(stderr, "Erro: %s\n", error);
        return 1;
    }
    printf("Imagem enviada para %s (%s, %u pontos).\n", device,
           mode == ESCPOS_IMAGE_RASTER ? "raster" : "bit-image", width);
    return 0;
}
