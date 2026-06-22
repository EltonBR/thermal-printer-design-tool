#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "escpos.h"

#define MAX_REQUEST_SIZE (16U * 1024U * 1024U)

static volatile sig_atomic_t running = 1;
static const char *printer_path = "/dev/usb/lp0";
static bool regular_output = false;

static void stop_server(int signal_number) {
    (void)signal_number;
    running = 0;
}

static bool write_all(int fd, const void *data, size_t length) {
    const uint8_t *cursor = data;
    while (length > 0) {
        ssize_t written = write(fd, cursor, length);
        if (written < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
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
        cursor += written;
        length -= (size_t)written;
    }
    return true;
}

static void send_response(int client, int status, const char *reason,
                          const char *type, const void *body, size_t body_length) {
    char header[512];
    int header_length = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
        status, reason, type, body_length);
    if (header_length > 0) {
        write_all(client, header, (size_t)header_length);
        write_all(client, body, body_length);
    }
}

static void send_json(int client, int status, const char *reason, const char *json) {
    send_response(client, status, reason, "application/json; charset=utf-8",
                  json, strlen(json));
}

static const char *mime_type(const char *path) {
    const char *extension = strrchr(path, '.');
    if (!extension) return "application/octet-stream";
    if (!strcmp(extension, ".html")) return "text/html; charset=utf-8";
    if (!strcmp(extension, ".css")) return "text/css; charset=utf-8";
    if (!strcmp(extension, ".js")) return "text/javascript; charset=utf-8";
    if (!strcmp(extension, ".png")) return "image/png";
    if (!strcmp(extension, ".jpg") || !strcmp(extension, ".jpeg")) return "image/jpeg";
    if (!strcmp(extension, ".ttf")) return "font/ttf";
    return "application/octet-stream";
}

static void serve_file(int client, const char *request_path) {
    char path[1024];
    const char *relative = !strcmp(request_path, "/") ? "index.html" : request_path + 1;
    if (strstr(relative, "..") || strchr(relative, '\\')) {
        send_json(client, 403, "Forbidden", "{\"error\":\"caminho inválido\"}");
        return;
    }
    if (snprintf(path, sizeof(path), "%s", relative) >= (int)sizeof(path)) {
        send_json(client, 414, "URI Too Long", "{\"error\":\"caminho muito longo\"}");
        return;
    }

    int file = open(path, O_RDONLY);
    struct stat metadata;
    if (file < 0 || fstat(file, &metadata) < 0 || !S_ISREG(metadata.st_mode)) {
        if (file >= 0) close(file);
        send_json(client, 404, "Not Found", "{\"error\":\"arquivo não encontrado\"}");
        return;
    }

    char header[512];
    int header_length = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %lld\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
        mime_type(path), (long long)metadata.st_size);
    write_all(client, header, (size_t)header_length);

    uint8_t buffer[16384];
    ssize_t count;
    while ((count = read(file, buffer, sizeof(buffer))) > 0) {
        if (!write_all(client, buffer, (size_t)count)) break;
    }
    close(file);
}

static void print_png(int client, const uint8_t *body, size_t body_length) {
    escpos_config config = escpos_default_config(printer_path);
    config.regular_file = regular_output;
    char error[256];
    if (!escpos_print_png(&config, body, body_length, error, sizeof(error))) {
        char json[512];
        snprintf(json, sizeof(json), "{\"error\":\"%s\"}", error);
        send_json(client, 500, "Internal Server Error", json);
        return;
    }
    send_json(client, 200, "OK", "{\"ok\":true}");
}

static char *find_header_end(char *data, size_t length) {
    if (length < 4) return NULL;
    for (size_t i = 0; i <= length - 4; i++) {
        if (!memcmp(data + i, "\r\n\r\n", 4)) return data + i;
    }
    return NULL;
}

static char *header_value(char *headers, const char *name) {
    size_t name_length = strlen(name);
    char *line = strstr(headers, "\r\n");
    while (line) {
        line += 2;
        if (!strncasecmp(line, name, name_length) && line[name_length] == ':') {
            char *value = line + name_length + 1;
            while (*value == ' ' || *value == '\t') value++;
            return value;
        }
        line = strstr(line, "\r\n");
    }
    return NULL;
}

static void handle_client(int client) {
    uint8_t *request = malloc(MAX_REQUEST_SIZE);
    if (!request) return;
    size_t received = 0;
    char *header_end = NULL;
    size_t header_length = 0;
    size_t content_length = 0;

    while (received < MAX_REQUEST_SIZE) {
        ssize_t count = read(client, request + received, MAX_REQUEST_SIZE - received);
        if (count <= 0) break;
        received += (size_t)count;
        if (header_length == 0) {
            header_end = find_header_end((char *)request, received);
        }
        if (header_end && header_length == 0) {
            header_length = (size_t)(header_end - (char *)request) + 4U;
            *header_end = '\0';
            char *length_header = header_value((char *)request, "Content-Length");
            if (length_header) content_length = strtoull(length_header, NULL, 10);
            if (content_length > MAX_REQUEST_SIZE - header_length) {
                send_json(client, 413, "Payload Too Large", "{\"error\":\"imagem muito grande\"}");
                free(request);
                return;
            }
        }
        if (header_length > 0 && received >= header_length + content_length) break;
    }

    if (!header_end) {
        send_json(client, 400, "Bad Request", "{\"error\":\"requisição inválida\"}");
        free(request);
        return;
    }

    char method[8], path[1024];
    if (sscanf((char *)request, "%7s %1023s", method, path) != 2) {
        send_json(client, 400, "Bad Request", "{\"error\":\"requisição inválida\"}");
        free(request);
        return;
    }
    char *query = strchr(path, '?');
    if (query) *query = '\0';
    if (!strcmp(method, "POST") && !strcmp(path, "/api/print")) {
        char *type = header_value((char *)request, "Content-Type");
        if (received < header_length + content_length) {
            send_json(client, 400, "Bad Request", "{\"error\":\"corpo da requisição incompleto\"}");
        } else if (!type || strncasecmp(type, "image/png", 9)) {
            send_json(client, 415, "Unsupported Media Type", "{\"error\":\"Content-Type deve ser image/png\"}");
        } else {
            print_png(client, request + header_length, content_length);
        }
    } else if (!strcmp(method, "GET")) {
        serve_file(client, path);
    } else {
        send_json(client, 404, "Not Found", "{\"error\":\"rota não encontrada\"}");
    }
    free(request);
}

static void usage(const char *program) {
    fprintf(stderr, "Uso: %s [--port PORTA] [--device /dev/usb/lp0 | --output arquivo]\n", program);
}

int main(int argc, char **argv) {
    unsigned long port = 8080;
    const char *environment_device = getenv("PRINTER_DEVICE");
    if (environment_device && *environment_device) printer_path = environment_device;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) {
            port = strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--device") && i + 1 < argc) {
            printer_path = argv[++i];
            regular_output = false;
        } else if (!strcmp(argv[i], "--output") && i + 1 < argc) {
            printer_path = argv[++i];
            regular_output = true;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (port == 0 || port > 65535) {
        usage(argv[0]);
        return 2;
    }

    signal(SIGINT, stop_server);
    signal(SIGTERM, stop_server);
    signal(SIGPIPE, SIG_IGN);

    int server = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons((uint16_t)port),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
    };
    if (server < 0 || bind(server, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(server, 16) < 0) {
        perror("não foi possível iniciar o servidor");
        if (server >= 0) close(server);
        return 1;
    }

    printf("Servidor em http://127.0.0.1:%lu (impressora: %s)\n", port, printer_path);
    fflush(stdout);
    while (running) {
        int client = accept(server, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        handle_client(client);
        close(client);
    }
    close(server);
    return 0;
}
