// Test-only backend. It is never linked into the engine or release artifacts.
#include "lite/lite.h"
#include "import.h"
#include <cstdarg>
TokenCache g_tokens;
struct shared_ptr g_reqCtx;
bool g_ssl_verify_disabled = false, g_code_from_file = false;
void TokenCache::load() { dev_token = "fixture-dev"; music_token = "fixture-music"; storefront_id = "us"; }
void TokenCache::save() {}
void install_hooks() {}
struct shared_ptr init_ctx() { return {}; }
bool login(struct shared_ptr) { return true; }
void set_device_info(const char*) {}
void set_base_dir(const char*) {}
void set_credentials(const char*, const char*) {}
void setup_services() {}
bool cache_login_tokens() { return true; }
bool refresh_tokens(std::string&, std::string&, std::string&) { return false; }
std::string get_storefront() { return "us"; }
std::string get_music_token() { return "fixture-music"; }
std::string get_dev_token() { return "fixture-dev"; }
std::string fetch_dev_token() { return "fixture-dev"; }
std::string get_m3u8(const std::string&) { return "https://example.com/audio.m3u8"; }
std::string get_key(const std::string&, const std::string&, uint8_t* ctx, uint8_t* state,
                    uint64_t* rcx, uint64_t* rax, uint64_t* rdx, uint64_t* r9, uint64_t* rbp) {
    memset(ctx, 0, 0x8000); memset(state, 0, 0x2100);
    *rcx = *rax = *rdx = *r9 = *rbp = 1;
    return "fixture-content-key";
}
std::string AppleApi::getDevToken() { return "fixture-web-dev"; }
std::string AppleApi::getLyrics(const std::string&, const std::string&, const std::string&, bool,
                               const std::string&, const std::string&, const std::string&) {
    return "<tt>fixture lyrics</tt>";
}
std::string AppleApi::getWebPlayback(const std::string&, const std::string&, const std::string&) {
    return "https://example.com/web.m3u8";
}
bool AppleApi::getLicense(const std::string&, const std::string&, const std::string&, const std::string&,
                          const std::string&, const std::string&, std::string& license, int& renew) {
    license = "bGljZW5zZQ=="; renew = 0; return true;
}
