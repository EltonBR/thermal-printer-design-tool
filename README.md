# Thermal Printer Design Tool

Editor web para criar conteúdo em Markdown ou HTML, visualizar o resultado e
imprimir diretamente em impressoras térmicas compatíveis com ESC/POS.

## Recursos

- Editor Markdown/HTML com pré-visualização.
- Exportação do conteúdo como PNG.
- Impressão por um backend HTTP escrito em C.
- Biblioteca ESC/POS independente para uso pelo servidor ou pela linha de comando.
- Conversão de imagens coloridas para 1 bit com dithering Floyd–Steinberg.
- Largura padrão de 384 pontos para impressoras térmicas de 58 mm.
- Operação local e sem dependências JavaScript externas.

## Requisitos

- Linux
- Compilador C e `make`
- `pkg-config`
- Biblioteca e headers da `libpng`

Instalação automática das dependências:

```sh
./install-deps.sh
```

Para apenas verificar o ambiente, sem instalar pacotes:

```sh
./install-deps.sh --check
```

O script suporta Debian/Ubuntu (`apt-get`), Fedora/RHEL (`dnf`) e Arch Linux
(`pacman`).

Em sistemas baseados em Debian ou Ubuntu:

```sh
sudo apt install build-essential pkg-config libpng-dev
```

## Executando

Conecte a impressora e inicie o servidor:

```sh
./start.sh
```

Por padrão, o servidor usa:

- Endereço: `http://127.0.0.1:8080`
- Dispositivo: `/dev/usb/lp3`

Abra o endereço no navegador, edite o conteúdo e clique em **Imprimir**.

Para alterar a porta ou o dispositivo:

```sh
PORT=8081 PRINTER_DEVICE=/dev/usb/lp0 ./start.sh
```

O usuário que executa o servidor precisa ter permissão de escrita no dispositivo
da impressora.

## Impressão direta

Para testar uma imagem sem navegador ou HTTP:

```sh
make
./escpos-print imagem.png --device /dev/usb/lp3 --mode raster --width 384
```

Modos disponíveis:

- `raster`: comando `GS v 0`, usado por padrão.
- `bit-image`: comando `ESC *` em formato de colunas.

## Biblioteca ESC/POS

O header público está em `server/lib/escpos.h`. A biblioteca mantém uma conexão
com o dispositivo aberta para que texto, imagens e comandos façam parte do mesmo
trabalho de impressão.

A referência completa, observações de compatibilidade e exemplos independentes
estão em [docs/ESCPOS_API.md](docs/ESCPOS_API.md).

```c
#include "escpos.h"

escpos_printer printer = {.fd = -1};
char error[256];

if (!escpos_open(&printer, "/dev/usb/lp3", error, sizeof(error))) {
    /* tratar error */
}

escpos_initialize(&printer);
escpos_set_align(&printer, ESCPOS_ALIGN_CENTER);
escpos_set_bold(&printer, true);
escpos_set_text_size(&printer, 2, 2);
escpos_text_line(&printer, "MINHA LOJA");

escpos_set_bold(&printer, false);
escpos_set_text_size(&printer, 1, 1);
escpos_text_line(&printer, "Obrigado pela compra");
escpos_print_qr(&printer, "https://example.com", 19, 4, ESCPOS_QR_EC_M);
escpos_feed(&printer, 4);
escpos_close(&printer);
```

Funções disponíveis:

- Conexão persistente, fechamento e envio de bytes brutos.
- Inicialização, texto, linha de texto e avanço de papel.
- Alinhamento, fonte A/B, negrito, sublinhado e impressão invertida.
- Multiplicadores independentes de largura e altura do texto, de 1 a 8.
- Imagens PNG em raster ou colunas, redimensionamento e dithering 1-bit.
- EAN-13, EAN-8, UPC-A, UPC-E, CODE39, ITF, CODABAR e CODE128.
- QR Code nativo com tamanho e correção L/M/Q/H.
- Corte total/parcial e pulso de gaveta nos pinos 2 ou 5.

Corte, gaveta, tipos de código de barras e QR nativo dependem das capacidades do
modelo da impressora. Para CODE128, inclua no conteúdo o seletor do conjunto,
como `{B` ou `{C`, conforme o padrão ESC/POS.

## Testes

```sh
make test
```

Os testes validam imagens, estilos, alimentação, corte, gaveta, EAN-13 e QR Code
contra os vetores do [python-escpos](https://github.com/python-escpos/python-escpos),
além da conversão de tons intermediários para uma imagem 1-bit.

### Teste físico de capacidades

O binário `escpos-capabilities` imprime um relatório com alinhamentos, estilos,
fontes, tamanhos, EAN-13, CODE39, CODE128, QR Code e imagem 1-bit:

```sh
make
./escpos-capabilities --device /dev/usb/lp3 --image div-como-imagem.png
```

Por segurança, corte e gaveta não são acionados por padrão. Para incluí-los:

```sh
./escpos-capabilities --device /dev/usb/lp3 \
  --image div-como-imagem.png --cut --drawer 2
```

Use `--drawer 5` quando a gaveta estiver conectada ao pino 5. Os comandos
opcionais devem ser usados somente quando o hardware declarar suporte.

Para testar o servidor sem uma impressora física:

```sh
OUTPUT_FILE=/tmp/escpos.bin ./start.sh
```

## API HTTP

O backend serve o frontend e os arquivos em `assets/`. A impressão é feita por:

```text
POST /api/print
Content-Type: image/png
```

O PNG recebido é composto sobre fundo branco, redimensionado para no máximo 384
pontos, convertido para 1 bit e enviado à impressora como ESC/POS.

## Estrutura

- `server/backend.c`: servidor HTTP e arquivos estáticos.
- `server/lib/escpos.c` / `escpos.h`: conversão e transporte ESC/POS.
- `server/escpos-print.c`: cliente de impressão direta.
- `server/escpos-capabilities.c`: relatório físico das capacidades ESC/POS.
- `server/tests/escpos-test.c`: testes da codificação e do dithering.
- `index.html` / `assets/`: interface web e dependências estáticas.
- `start.sh`: compilação e inicialização do servidor.
- `install-deps.sh`: verificação e instalação das dependências nativas.
