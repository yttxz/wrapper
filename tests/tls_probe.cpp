#include "lite.h"
bool g_ssl_verify_disabled = false;
static size_t discard(char*, size_t size, size_t count, void*) { return size * count; }
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    setenv("ANDROID_DATA", "/data", 1);
    setenv("ANDROID_ROOT", "/system", 1);
    CurlEasy curl;
    curl.setOptStr(CURLOPT_URL, argv[1]);
    curl.setOptPtr(CURLOPT_WRITEFUNCTION, reinterpret_cast<void*>(discard));
    curl.setOptInt(CURLOPT_TIMEOUT_MS, 5000);
    return curl.perform() ? 0 : 1;
}
