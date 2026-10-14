#ifndef WRAPPER_KEY_REQUEST_H
#define WRAPPER_KEY_REQUEST_H

#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

#define WRAPPER_MAX_KEY_REQUEST_BYTES 4096
#define WRAPPER_MAX_KEY_ADAM_ID_BYTES 32
#define WRAPPER_MAX_KEY_URI_BYTES 240
#define WRAPPER_PREFETCH_KEY_URI "skd://itunes.apple.com/P000000000/s1/e1"

struct wrapper_key_request {
    char adam_id[WRAPPER_MAX_KEY_ADAM_ID_BYTES + 1];
    char uri[WRAPPER_MAX_KEY_URI_BYTES + 1];
};

static inline int wrapper_read_key_request(int fd, char *buffer) {
    size_t used = 0;
    while (used < WRAPPER_MAX_KEY_REQUEST_BYTES) {
        ssize_t count = read(fd, buffer + used, WRAPPER_MAX_KEY_REQUEST_BYTES - used);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return 400;
        if (memchr(buffer + used, '\0', (size_t)count)) return 400;
        used += (size_t)count;
        buffer[used] = '\0';
        if (strstr(buffer, "\r\n\r\n")) return 200;
    }
    return 413;
}

static inline int wrapper_query_decode(const char *src, char *out, size_t capacity) {
    size_t used = 0;
    while (*src) {
        unsigned char ch = (unsigned char)*src++;
        if (ch == '%') {
            if (!src[0] || !src[1] || !isxdigit((unsigned char)src[0]) ||
                !isxdigit((unsigned char)src[1])) return 0;
            int hi = isdigit((unsigned char)src[0]) ? src[0] - '0' :
                     tolower((unsigned char)src[0]) - 'a' + 10;
            int lo = isdigit((unsigned char)src[1]) ? src[1] - '0' :
                     tolower((unsigned char)src[1]) - 'a' + 10;
            ch = (unsigned char)((hi << 4) | lo);
            src += 2;
        } else if (ch == '+') {
            ch = ' ';
        }
        if (ch <= 0x20 || ch >= 0x7f || used + 1 >= capacity) return 0;
        out[used++] = (char)ch;
    }
    out[used] = '\0';
    return used != 0;
}

/* Parse only the request target; headers cannot supply query parameters. */
static inline int wrapper_parse_key_request(char *buffer, struct wrapper_key_request *out) {
    char *line_end = strstr(buffer, "\r\n");
    if (!line_end) return 400;
    *line_end = '\0';
    char *target = strchr(buffer, ' ');
    if (!target) return 400;
    *target++ = '\0';
    if (strcmp(buffer, "GET") != 0) return 405;
    char *version = strchr(target, ' ');
    if (!version) return 400;
    *version++ = '\0';
    if (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0) return 400;
    char *query = strchr(target, '?');
    if (query) *query++ = '\0';
    if (strcmp(target, "/") != 0 && strcmp(target, "/key") != 0) return 404;
    if (!query || !*query) return 400;
    out->adam_id[0] = out->uri[0] = '\0';
    while (query && *query) {
        char *next = strchr(query, '&');
        if (next) *next++ = '\0';
        if (strncmp(query, "adamId=", 7) == 0) {
            if (out->adam_id[0] || !wrapper_query_decode(query + 7, out->adam_id,
                                                       sizeof(out->adam_id))) return 400;
        } else if (strncmp(query, "uri=", 4) == 0) {
            if (out->uri[0] || !wrapper_query_decode(query + 4, out->uri,
                                                   sizeof(out->uri))) return 400;
        }
        query = next;
    }
    if (!out->adam_id[0] || strncmp(out->uri, "skd://", 6) != 0 || !out->uri[6]) return 400;
    for (const char *p = out->adam_id; *p; p++) {
        if (!isdigit((unsigned char)*p)) return 400;
    }
    /* Match lite's fix: a real track must supply its own content key URI. */
    if (strcmp(out->adam_id, "0") != 0 && strcmp(out->uri, WRAPPER_PREFETCH_KEY_URI) == 0) return 400;
    return 200;
}

#endif
