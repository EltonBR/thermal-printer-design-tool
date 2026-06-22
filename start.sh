#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$PROJECT_DIR"

PORT=${PORT:-8080}
PRINTER_DEVICE=${PRINTER_DEVICE:-/dev/usb/lp3}

if ! command -v make >/dev/null 2>&1; then
    echo "Erro: make não está instalado." >&2
    exit 1
fi

if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists libpng; then
    echo "Erro: libpng e pkg-config são necessários para compilar o backend." >&2
    exit 1
fi

make

if [ -n "${OUTPUT_FILE:-}" ]; then
    exec ./printer-server --port "$PORT" --output "$OUTPUT_FILE"
fi

exec ./printer-server --port "$PORT" --device "$PRINTER_DEVICE"
