#!/bin/sh
set -eu

check_dependencies() {
    missing=""

    for command in cc make pkg-config; do
        if ! command -v "$command" >/dev/null 2>&1; then
            missing="$missing $command"
        fi
    done

    if command -v pkg-config >/dev/null 2>&1 && ! pkg-config --exists libpng; then
        missing="$missing libpng"
    fi

    if [ -n "$missing" ]; then
        echo "Dependências ausentes:$missing" >&2
        return 1
    fi

    echo "Todas as dependências estão instaladas."
}

run_as_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        echo "Erro: execute como root ou instale o comando sudo." >&2
        exit 1
    fi
}

if [ "${1:-}" = "--check" ]; then
    check_dependencies
    exit $?
fi

if [ "$#" -ne 0 ]; then
    echo "Uso: $0 [--check]" >&2
    exit 2
fi

if check_dependencies >/dev/null 2>&1; then
    echo "Todas as dependências já estão instaladas."
    exit 0
fi

if command -v apt-get >/dev/null 2>&1; then
    run_as_root apt-get update
    run_as_root apt-get install -y build-essential pkg-config libpng-dev
elif command -v dnf >/dev/null 2>&1; then
    run_as_root dnf install -y gcc make pkgconf-pkg-config libpng-devel
elif command -v pacman >/dev/null 2>&1; then
    run_as_root pacman -Sy --needed --noconfirm base-devel pkgconf libpng
else
    echo "Gerenciador de pacotes não suportado." >&2
    echo "Instale manualmente: compilador C, make, pkg-config e libpng (headers)." >&2
    exit 1
fi

check_dependencies
