#include "lite/login_input.h"
#include "lite/request_validation.h"
#include "lite/private_cache.h"
#include <cassert>
#include <fstream>
#include <sys/stat.h>
int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string dir = argv[1];
    assert(mkdir(dir.c_str(), 0700) == 0);
    for (size_t length : {size_t(1), size_t(57), size_t(58), size_t(63), size_t(64), size_t(1023)}) {
        std::string password(length, 'x');
        assert(login_input::append_code(password, "123456") == password + "123456");
    }
    bool rejected = false;
    try { login_input::append_code(std::string(1024, 'x'), "123456"); }
    catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
    for (const auto& code : {"", "12345", "1234567", "12345a", "123 56"})
        assert(!login_input::valid_code(code));
    std::string code;
    const auto file = dir + "/2fa.txt";
    assert(login_input::read_code_file(file, code) == 0);
    auto write = [&](const std::string& value, int mode) {
        std::ofstream out(file); out << value; out.close(); assert(chmod(file.c_str(), mode) == 0);
    };
    write(" 123456\n", 0600);
    assert(login_input::read_code_file(file, code) == 1 && code == "123456");
    assert(access(file.c_str(), F_OK) != 0);
    write("12345a", 0600);
    assert(login_input::read_code_file(file, code) == 0 && code.empty());
    write(std::string("123\0abc", 7), 0600);
    assert(login_input::read_code_file(file, code) == 0);
    write("123456", 0644);
    assert(login_input::read_code_file(file, code) == -1);
    assert(unlink(file.c_str()) == 0);
    auto target = dir + "/target";
    { std::ofstream out(target); out << "123456"; }
    assert(symlink(target.c_str(), file.c_str()) == 0);
    assert(login_input::read_code_file(file, code) == -1);
    assert(unlink(file.c_str()) == 0);
    assert(mkfifo(file.c_str(), 0600) == 0);
    assert(login_input::read_code_file(file, code) == -1);
    assert(unlink(file.c_str()) == 0);
    assert(request_validation::adam_id("123"));
    assert(!request_validation::adam_id("123x"));
    assert(!request_validation::adam_id(std::string(33, '1')));
    assert(request_validation::key_uri("skd://example/track"));
    assert(!request_validation::key_uri("skd://example/\n"));
    assert(!request_validation::key_uri("https://example/track"));
    const std::string cache = dir + "/token";
    assert(private_cache::write(cache, "fixture-token\n"));
    assert(private_cache::read(cache) == "fixture-token");
    struct stat mode;
    assert(stat(cache.c_str(), &mode) == 0 && (mode.st_mode & 0777) == 0600);
    assert(private_cache::read(cache, 2).empty());
    assert(unlink(cache.c_str()) == 0);
    assert(symlink(target.c_str(), cache.c_str()) == 0);
    assert(private_cache::read(cache).empty());
    // Atomic replacement replaces the link itself; its target is not overwritten.
    assert(private_cache::write(cache, "new-cache"));
    assert(private_cache::read(target) == "123456");
    assert(private_cache::read(cache) == "new-cache");
    puts("login, request validation, and private-cache tests passed");
}
