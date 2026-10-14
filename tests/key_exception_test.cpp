#include <cassert>
#include <cstdint>
#include <future>
#include <stdexcept>
#include <thread>
#include <sys/socket.h>
#include "../key-request.h"

extern "C" uint8_t handle_cpp(int);
extern "C" uint8_t handle_key_request_cpp(int);
extern "C" void renew_playback_lease_cpp(void);

static int error_code, resets, calls, mode, renewals;
static std::promise<void> decrypt_started, decrypt_finished;

extern "C" void handle(int) {
    decrypt_started.set_value();
    decrypt_finished.get_future().wait();
}
extern "C" void handle_key_request(int, const struct wrapper_key_request *) {
    calls++;
    if (mode == 1) throw std::runtime_error("private vendor message");
    if (mode == 2) throw 1;
}
extern "C" void key_request_error(int, int code) { error_code = code; }
extern "C" void reset_key_capture(void) { resets++; }
extern "C" void renew_playback_lease(void) { renewals++; }

static uint8_t request_key() {
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    const char *request = "GET /key?adamId=123&uri=skd://example/track HTTP/1.1\r\n\r\n";
    assert(write(sockets[1], request, strlen(request)) == (ssize_t)strlen(request));
    uint8_t result = handle_key_request_cpp(sockets[0]);
    close(sockets[0]); close(sockets[1]);
    return result;
}

int main() {
    assert(request_key() == 1);
    mode = 1;
    assert(request_key() == 0 && error_code == 500 && resets == 1);
    mode = 2;
    assert(request_key() == 0 && error_code == 500 && resets == 2);
    mode = 0;
    assert(request_key() == 1); // Exception paths released the lock.
    std::thread decrypt([] { assert(handle_cpp(0) == 1); });
    decrypt_started.get_future().wait();
    int previous_calls = calls;
    assert(request_key() == 0 && error_code == 503);
    assert(calls == previous_calls); // No native work while decrypt owns contexts.
    decrypt_finished.set_value();
    decrypt.join();
    assert(request_key() == 1);
    renew_playback_lease_cpp();
    assert(renewals == 1);
}
