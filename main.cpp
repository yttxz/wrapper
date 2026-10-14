#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include "key-request.h"

extern "C" void handle(int fd);
extern "C" void renew_playback_lease(void);

// The key service can reset native contexts that a decrypt client still holds.
static std::mutex session_mutex;

extern "C" void renew_playback_lease_cpp(void) {
    std::lock_guard<std::mutex> lock(session_mutex);
    try { renew_playback_lease(); }
    catch (...) { fprintf(stderr, "[!] playback lease renewal failed\n"); }
}

extern "C" uint8_t handle_cpp(int fd) {
    std::lock_guard<std::mutex> lock(session_mutex);
    try {
        handle(fd);
        return 1;
    } catch (const std::exception &e) {
        fprintf(stderr, "[!] caught an exception: %s\n", e.what());
        return 0;
    }
}

extern "C" void handle_key_request(int fd, const struct wrapper_key_request *request);
extern "C" void key_request_error(int fd, int code);
extern "C" void reset_key_capture(void);

/* Always send a correctly framed response when the vendor library throws. */
extern "C" uint8_t handle_key_request_cpp(int fd) {
    // Read before acquiring the native-session lock; a slow HTTP client must not
    // block decrypt clients, and a busy response must not close unread headers.
    char buffer[WRAPPER_MAX_KEY_REQUEST_BYTES + 1];
    struct wrapper_key_request request;
    int status = wrapper_read_key_request(fd, buffer);
    if (status == 200) status = wrapper_parse_key_request(buffer, &request);
    if (status != 200) {
        key_request_error(fd, status);
        return 0;
    }
    std::unique_lock<std::mutex> lock(session_mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        key_request_error(fd, 503);
        return 0;
    }
    try {
        handle_key_request(fd, &request);
        return 1;
    } catch (...) {
        fprintf(stderr, "[!] key request failed with a native exception\n");
        reset_key_capture();
        key_request_error(fd, 500);
        return 0;
    }
}

static void endLeaseCb(int const &c) {
    fprintf(stderr, "[.] end lease code %d\n", c);
    exit(1);
}

static void pbErrCb(void *) {
    fprintf(stderr, "[.] playback error\n");
    exit(1);
}

extern "C" {
std::function<void (int const&)> endLeaseCallback(endLeaseCb);
std::function<void (void *)> pbErrCallback(pbErrCb);
}
