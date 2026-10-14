#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/socket.h>
#include <time.h>
#include "key-request.h"

static int parse(const char *input) {
    char buffer[WRAPPER_MAX_KEY_REQUEST_BYTES + 1];
    struct wrapper_key_request request;
    assert(strlen(input) < sizeof(buffer));
    strcpy(buffer, input);
    return wrapper_parse_key_request(buffer, &request);
}

static void *write_fragments(void *arg) {
    int fd = *(int *)arg;
    const char *parts[] = {
        "GET /key?adamId=123&uri=skd%3A%2F%2Fexample%2Ftrack HTTP/1.1\r\n",
        "Host: localhost\r\n", "\r\n"
    };
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        assert(write(fd, parts[i], strlen(parts[i])) == (ssize_t)strlen(parts[i]));
        struct timespec delay = {.tv_nsec = 20000000};
        nanosleep(&delay, NULL);
    }
    return NULL;
}

int main(void) {
    assert(parse("GET /key?adamId=123&uri=skd%3A%2F%2Fexample%2Ftrack HTTP/1.1\r\n\r\n") == 200);
    assert(parse("GET /?uri=skd://example/track&adamId=123 HTTP/1.0\r\n\r\n") == 200);
    assert(parse("GET /?adamId=0&uri=" WRAPPER_PREFETCH_KEY_URI " HTTP/1.1\r\n\r\n") == 200);
    assert(parse("GET /?adamId=123&uri=" WRAPPER_PREFETCH_KEY_URI " HTTP/1.1\r\n\r\n") == 400);
    assert(parse("GET /?adamId=123 HTTP/1.1\r\n\r\n") == 400);
    assert(parse("GET /?uri=skd://example/track HTTP/1.1\r\n\r\n") == 400);
    assert(parse("GET /?adamId=123&adamId=456&uri=skd://example/track HTTP/1.1\r\n\r\n") == 400);
    assert(parse("GET /?adamId=123&uri=skd://a&uri=skd://b HTTP/1.1\r\n\r\n") == 400);
    assert(parse("GET /?adamId=123%00&uri=skd://example/track HTTP/1.1\r\n\r\n") == 400);
    assert(parse("GET /?adamId=x&uri=skd://example/track HTTP/1.1\r\n\r\n") == 400);
    assert(parse("GET /?adamId=123&uri=skd://example/%0a HTTP/1.1\r\n\r\n") == 400);
    assert(parse("GET /?adamId=123&uri=skd://example/%q0 HTTP/1.1\r\n\r\n") == 400);
    assert(parse("GET /?adamId=123&uri=skd://example/% HTTP/1.1\r\n\r\n") == 400);
    assert(parse("GET /?adamId=123&uri=https://example/track HTTP/1.1\r\n\r\n") == 400);
    assert(parse("GET / HTTP/1.1\r\nX-Query: ?adamId=123&uri=skd://example/track\r\n\r\n") == 400);
    assert(parse("POST /?adamId=123&uri=skd://example/track HTTP/1.1\r\n\r\n") == 405);
    assert(parse("GETTING /?adamId=123&uri=skd://example/track HTTP/1.1\r\n\r\n") == 405);
    assert(parse("GET /other?adamId=123&uri=skd://example/track HTTP/1.1\r\n\r\n") == 404);
    assert(parse("GET /?adamId=123&uri=skd://example/track\r\n\r\n") == 400);

    char buffer[WRAPPER_MAX_KEY_REQUEST_BYTES + 1];
    char adam[WRAPPER_MAX_KEY_ADAM_ID_BYTES + 2];
    memset(adam, '1', sizeof(adam) - 1);
    adam[sizeof(adam) - 1] = '\0';
    snprintf(buffer, sizeof(buffer), "GET /?adamId=%s&uri=skd://example/track HTTP/1.1\r\n\r\n", adam);
    assert(parse(buffer) == 400);
    adam[sizeof(adam) - 2] = '\0';
    snprintf(buffer, sizeof(buffer), "GET /?adamId=%s&uri=skd://example/track HTTP/1.1\r\n\r\n", adam);
    assert(parse(buffer) == 200);
    char uri[WRAPPER_MAX_KEY_URI_BYTES + 2];
    memset(uri, 'a', sizeof(uri) - 1);
    memcpy(uri, "skd://", 6);
    uri[sizeof(uri) - 1] = '\0';
    snprintf(buffer, sizeof(buffer), "GET /?adamId=123&uri=%s HTTP/1.1\r\n\r\n", uri);
    assert(parse(buffer) == 400);
    uri[sizeof(uri) - 2] = '\0';
    snprintf(buffer, sizeof(buffer), "GET /?adamId=123&uri=%s HTTP/1.1\r\n\r\n", uri);
    assert(parse(buffer) == 200);

    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    pthread_t writer;
    assert(pthread_create(&writer, NULL, write_fragments, &sockets[1]) == 0);
    assert(wrapper_read_key_request(sockets[0], buffer) == 200);
    assert(parse(buffer) == 200);
    assert(pthread_join(writer, NULL) == 0);
    close(sockets[0]); close(sockets[1]);

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    memset(buffer, 'a', WRAPPER_MAX_KEY_REQUEST_BYTES);
    assert(write(sockets[1], buffer, WRAPPER_MAX_KEY_REQUEST_BYTES) == WRAPPER_MAX_KEY_REQUEST_BYTES);
    assert(wrapper_read_key_request(sockets[0], buffer) == 413);
    close(sockets[0]); close(sockets[1]);

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(write(sockets[1], "GET /\0bad", 9) == 9);
    assert(wrapper_read_key_request(sockets[0], buffer) == 400);
    close(sockets[0]); close(sockets[1]);
    puts("key request tests passed");
    return 0;
}
