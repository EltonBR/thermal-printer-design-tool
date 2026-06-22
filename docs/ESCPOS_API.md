# Referência da biblioteca ESC/POS

Esta documentação descreve a API pública definida em
`server/lib/escpos.h`. A biblioteca envia comandos ESC/POS diretamente para um
dispositivo Linux, como `/dev/usb/lp3`, e converte imagens PNG para impressão
térmica monocromática.

## Compilação

A biblioteca requer `libpng` e um compilador compatível com C99 ou superior.

No repositório, as dependências podem ser verificadas ou instaladas com:

```sh
./install-deps.sh --check
./install-deps.sh
```

```sh
cc -O2 -Wall -Wextra -Wpedantic \
  -Iserver/lib $(pkg-config --cflags libpng) \
  programa.c server/lib/escpos.c \
  $(pkg-config --libs libpng) -o programa
```

No projeto, `make` gera os seguintes binários:

- `printer-server`: servidor HTTP e frontend.
- `escpos-print`: impressão direta de um PNG.
- `escpos-capabilities`: relatório físico de capacidades.
- `escpos-test`: testes automatizados dos bytes ESC/POS.

## Tratamento de erros

As operações retornam `true` em caso de sucesso e `false` em caso de erro.
Erros de sistema e parâmetros inválidos também são registrados em `errno`.

`escpos_open` e as funções de imagem recebem um buffer textual para detalhes:

```c
char error[256] = {0};

if (!escpos_open(&printer, "/dev/usb/lp3", error, sizeof(error))) {
    fprintf(stderr, "Erro: %s\n", error);
}
```

Inicialize `escpos_printer.fd` com `-1` antes de abrir a conexão. Sempre chame
`escpos_close`, inclusive depois de um erro ocorrido durante a impressão.

## Tipos públicos

### `escpos_printer`

Representa uma conexão aberta com uma impressora.

```c
typedef struct {
    int fd;
} escpos_printer;
```

### `escpos_config`

Configuração usada na conversão e impressão de imagens.

| Campo | Descrição |
|---|---|
| `device` | Caminho do dispositivo ou arquivo de saída. |
| `max_width` | Largura máxima da imagem em pontos. O padrão é `384`. |
| `threshold` | Limiar do dithering, de 0 a 255. O padrão é `128`. |
| `image_mode` | `ESCPOS_IMAGE_RASTER` ou `ESCPOS_IMAGE_BIT_IMAGE`. |
| `regular_file` | Cria/trunca um arquivo comum quando `true`; use apenas em testes. |

Use `escpos_default_config` para obter valores iniciais seguros.

### Enumerações

Alinhamento:

- `ESCPOS_ALIGN_LEFT`
- `ESCPOS_ALIGN_CENTER`
- `ESCPOS_ALIGN_RIGHT`

Corte:

- `ESCPOS_CUT_FULL`
- `ESCPOS_CUT_PARTIAL`

Imagens:

- `ESCPOS_IMAGE_RASTER`: `GS v 0`; modo padrão e validado na MIAOBAO 58Printer.
- `ESCPOS_IMAGE_BIT_IMAGE`: `ESC *`; alternativa para modelos legados.

Códigos de barras:

- `ESCPOS_BARCODE_UPCA`
- `ESCPOS_BARCODE_UPCE`
- `ESCPOS_BARCODE_EAN13`
- `ESCPOS_BARCODE_EAN8`
- `ESCPOS_BARCODE_CODE39`
- `ESCPOS_BARCODE_ITF`
- `ESCPOS_BARCODE_CODABAR`
- `ESCPOS_BARCODE_CODE128`

Correção de erro do QR Code:

- `ESCPOS_QR_EC_L`: aproximadamente 7%.
- `ESCPOS_QR_EC_M`: aproximadamente 15%.
- `ESCPOS_QR_EC_Q`: aproximadamente 25%.
- `ESCPOS_QR_EC_H`: aproximadamente 30%.

## Conexão e comandos básicos

### `escpos_open`

```c
bool escpos_open(escpos_printer *printer, const char *device,
                 char *error, size_t error_size);
```

Abre o dispositivo para escrita. O usuário do processo precisa ter permissão de
escrita no caminho informado.

### `escpos_close`

```c
void escpos_close(escpos_printer *printer);
```

Fecha o descritor e define `fd` como `-1`.

### `escpos_raw`

```c
bool escpos_raw(escpos_printer *printer, const void *data, size_t length);
```

Envia bytes sem interpretação. Essa função permite usar comandos específicos de
um fabricante que ainda não estejam representados pela API.

### `escpos_initialize`

```c
bool escpos_initialize(escpos_printer *printer);
```

Envia `ESC @`, limpa o buffer e restaura os modos padrão da impressora.

### Texto e avanço

```c
bool escpos_text(escpos_printer *printer, const char *text);
bool escpos_text_line(escpos_printer *printer, const char *text);
bool escpos_feed(escpos_printer *printer, unsigned lines);
```

`escpos_text_line` acrescenta `LF`. `escpos_feed` aceita de 0 a 255 linhas e usa
`ESC d n`.

A biblioteca não converte UTF-8 para a página de código interna da impressora.
Caracteres ASCII são portáveis; acentos dependem da página de código previamente
selecionada pelo equipamento ou por um comando enviado com `escpos_raw`.

## Formatação de texto

```c
bool escpos_set_align(escpos_printer *printer, escpos_align align);
bool escpos_set_bold(escpos_printer *printer, bool enabled);
bool escpos_set_underline(escpos_printer *printer, unsigned thickness);
bool escpos_set_invert(escpos_printer *printer, bool enabled);
bool escpos_set_font(escpos_printer *printer, unsigned font);
bool escpos_set_text_size(escpos_printer *printer,
                          unsigned width, unsigned height);
```

- Sublinhado aceita `0` (desligado), `1` ou `2` pontos.
- Fonte aceita `0` (A) ou `1` (B).
- Largura e altura aceitam multiplicadores independentes entre 1 e 8.
- Os estilos permanecem ativos até serem alterados ou até `escpos_initialize`.

## Imagens

### Configuração padrão

```c
escpos_config escpos_default_config(const char *device);
```

Retorna configuração raster, largura de 384 pontos e limiar 128.

### PNG em memória com conexão existente

```c
bool escpos_printer_print_png(escpos_printer *printer,
                              const escpos_config *config,
                              const uint8_t *png_data,
                              size_t png_length,
                              char *error,
                              size_t error_size);
```

Use esta função quando texto e imagem fizerem parte do mesmo recibo.

### PNG em uma operação independente

```c
bool escpos_print_png(const escpos_config *config,
                      const uint8_t *png_data,
                      size_t png_length,
                      char *error,
                      size_t error_size);

bool escpos_print_png_file(const escpos_config *config,
                           const char *png_path,
                           char *error,
                           size_t error_size);
```

Essas funções abrem e fecham o destino automaticamente.

O processamento de imagens:

1. Decodifica PNG com `libpng`.
2. Compõe transparência sobre fundo branco.
3. Redimensiona proporcionalmente para `max_width` quando necessário.
4. Converte luminosidade para 1 bit com dithering Floyd–Steinberg.
5. Empacota os pixels no formato ESC/POS selecionado.

A API de imagem aceita PNG. JPEG, GIF e WebP devem ser convertidos para PNG
antes da chamada. No frontend, `html2canvas` já produz um PNG.

## Códigos de barras

```c
bool escpos_print_barcode(escpos_printer *printer,
                          escpos_barcode type,
                          const char *data,
                          unsigned height,
                          unsigned width,
                          bool text_below);
```

- `height`: 1 a 255 pontos.
- `width`: 2 a 6.
- `text_below`: imprime ou oculta a representação legível.
- O comando centraliza o código de barras.
- O conteúdo deve ter no máximo 255 bytes.

A função valida limites, mas não calcula dígitos verificadores nem valida o
formato específico de cada simbologia. Para CODE128, informe o conjunto no
conteúdo, por exemplo `{BABC123` ou `{C001122`.

## QR Code nativo

```c
bool escpos_print_qr(escpos_printer *printer,
                     const void *data,
                     size_t length,
                     unsigned size,
                     escpos_qr_ec correction);
```

- Usa QR Code modelo 2 nativo, via `GS ( k`.
- `size` aceita valores de 1 a 16.
- O conteúdo pode ter até 65.530 bytes, embora impressoras normalmente imponham
  limites menores.
- Conteúdo vazio não envia comandos.

Nem todas as impressoras ESC/POS implementam QR nativo. Quando não houver
suporte, gere o QR como PNG e use a API de imagem.

## Corte e gaveta

```c
bool escpos_cut(escpos_printer *printer,
                escpos_cut_mode mode,
                bool feed_first);

bool escpos_cash_drawer(escpos_printer *printer, unsigned pin);
```

Quando `feed_first` for `true`, o corte avança seis linhas antes do comando. O
pulso de gaveta aceita os pinos `2` ou `5`.

Esses comandos só devem ser usados quando o hardware declarar suporte. A maioria
das impressoras portáteis ou modelos simples de 58 mm não possui cortador nem
conector de gaveta.

## Exemplo completo: recibo com texto, EAN-13 e QR Code

Salve como `recibo.c`:

```c
#include "escpos.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    escpos_printer printer = {.fd = -1};
    char error[256] = {0};

    if (!escpos_open(&printer, "/dev/usb/lp3", error, sizeof(error))) {
        fprintf(stderr, "Erro: %s\n", error);
        return 1;
    }

    bool ok =
        escpos_initialize(&printer) &&
        escpos_set_align(&printer, ESCPOS_ALIGN_CENTER) &&
        escpos_set_bold(&printer, true) &&
        escpos_set_text_size(&printer, 2, 2) &&
        escpos_text_line(&printer, "MINHA LOJA") &&
        escpos_set_bold(&printer, false) &&
        escpos_set_text_size(&printer, 1, 1) &&
        escpos_text_line(&printer, "Pedido 000123") &&
        escpos_text_line(&printer, "------------------------------") &&
        escpos_set_align(&printer, ESCPOS_ALIGN_LEFT) &&
        escpos_text_line(&printer, "Produto A              10,00") &&
        escpos_text_line(&printer, "Produto B               5,50") &&
        escpos_set_bold(&printer, true) &&
        escpos_text_line(&printer, "TOTAL                   15,50") &&
        escpos_set_bold(&printer, false) &&
        escpos_print_barcode(&printer, ESCPOS_BARCODE_EAN13,
                             "4006381333931", 64, 3, true) &&
        escpos_text_line(&printer, "") &&
        escpos_print_qr(&printer, "PEDIDO:000123", 13,
                        4, ESCPOS_QR_EC_M) &&
        escpos_feed(&printer, 4);

    if (!ok) perror("Falha durante a impressão");
    escpos_close(&printer);
    return ok ? 0 : 1;
}
```

Compile e execute:

```sh
cc -O2 -Iserver/lib $(pkg-config --cflags libpng) \
  recibo.c server/lib/escpos.c $(pkg-config --libs libpng) -o recibo
./recibo
```

## Exemplo completo: imprimir um PNG

Salve como `imagem.c`:

```c
#include "escpos.h"

#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s imagem.png\n", argv[0]);
        return 2;
    }

    escpos_config config = escpos_default_config("/dev/usb/lp3");
    config.max_width = 384;
    config.image_mode = ESCPOS_IMAGE_RASTER;

    char error[256] = {0};
    if (!escpos_print_png_file(&config, argv[1], error, sizeof(error))) {
        fprintf(stderr, "Erro: %s\n", error);
        return 1;
    }
    return 0;
}
```

## Exemplo completo: comando específico do fabricante

```c
#include "escpos.h"

#include <stdio.h>

int main(void) {
    escpos_printer printer = {.fd = -1};
    char error[256] = {0};
    if (!escpos_open(&printer, "/dev/usb/lp3", error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }

    /* BEL: algumas impressoras interpretam como beep. */
    const unsigned char beep = 0x07;
    bool ok = escpos_raw(&printer, &beep, 1);
    escpos_close(&printer);
    return ok ? 0 : 1;
}
```

## Diagnóstico de capacidades

Para imprimir um relatório que exercita estilos, fontes, tamanhos, códigos de
barras, QR Code e imagem:

```sh
./escpos-capabilities --device /dev/usb/lp3 \
  --image div-como-imagem.png
```

Corte e gaveta são opt-in:

```sh
./escpos-capabilities --device /dev/usb/lp3 \
  --image div-como-imagem.png --cut --drawer 2
```

## Observações de integração

- A biblioteca não é thread-safe para chamadas simultâneas na mesma conexão.
  Serialize o acesso a cada `escpos_printer`.
- Não intercale gravações de processos diferentes no mesmo `/dev/usb/lpN`.
- Mantenha o dispositivo aberto durante um recibo para preservar a ordem dos
  comandos.
- Estados de estilo pertencem à impressora e persistem entre chamadas. Restaure
  explicitamente negrito, sublinhado, inversão e tamanho quando necessário.
- O sucesso de `write` confirma que o kernel recebeu os bytes, não que o recurso
  físico existe. Use `escpos-capabilities` para verificar o modelo.
- A largura de 384 pontos é comum em mecanismos de 58 mm, mas não universal.
- Impressão térmica é monocromática. O dithering representa tons intermediários
  por densidade de pontos; não cria níveis reais de cinza.
- Evite áreas pretas extensas: elas aumentam consumo, temperatura e risco de
  falhas por limitação da fonte de alimentação.
- `regular_file=true` é destinado a testes e captura de bytes, não ao dispositivo
  `/dev/usb/lpN`.

## Referências

- [python-escpos](https://github.com/python-escpos/python-escpos): referência dos
  formatos de imagem e vetores de comandos usados nos testes.
- Comandos ESC/POS variam entre fabricantes. Consulte também o manual do modelo
  específico antes de usar corte, gaveta, QR Code ou extensões proprietárias.
