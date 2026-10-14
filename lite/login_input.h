#pragma once
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

namespace login_input {
struct Error : std::runtime_error { explicit Error(const char* message) : std::runtime_error(message) {} };
constexpr size_t max_password = 1023;
inline void wipe(std::string& text) {
    for (size_t i = 0; i < text.size(); ++i)
        *reinterpret_cast<volatile char*>(&text[i]) = 0;
    text.clear();
}
inline bool valid_code(const std::string& code) {
    if (code.size() != 6) return false;
    for (unsigned char c : code) if (c < '0' || c > '9') return false;
    return true;
}
inline std::string append_code(const std::string& password, const std::string& code) {
    if (password.empty() || password.size() > max_password || !valid_code(code))
        throw Error("invalid password length or 2FA code");
    return password + code;
}
inline std::string hidden_line(const char* prompt, size_t limit) {
    if (!isatty(STDIN_FILENO))
        throw Error("interactive login requires a TTY; use an explicit legacy password or file-based 2FA");
    struct Terminal {
        termios saved;
        bool active = false;
        Terminal() {
            if (tcgetattr(STDIN_FILENO, &saved) != 0) throw Error("cannot read terminal settings");
            termios hidden = saved;
            hidden.c_lflag &= ~ECHO;
            if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden) != 0) throw Error("cannot hide terminal input");
            active = true;
        }
        ~Terminal() { if (active) tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved); }
    } terminal;
    fprintf(stderr, "%s", prompt);
    fflush(stderr);
    std::string value;
    bool invalid = false;
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {
        if (c == 0 || value.size() >= limit) invalid = true;
        else value += static_cast<char>(c);
    }
    fprintf(stderr, "\n");
    if (!value.empty() && value.back() == '\r') value.pop_back();
    if (invalid || value.empty()) { wipe(value); throw Error("empty or oversized login input"); }
    return value;
}
// -1: unsafe/error; 0: absent or malformed (keep waiting); 1: valid and consumed.
inline int read_code_file(const std::string& path, std::string& code) {
    int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || (st.st_mode & 077) != 0) {
        close(fd); return -1;
    }
    char raw[65] = {};
    ssize_t count = -1;
    if (st.st_size >= 0 && st.st_size <= 64) {
        do { count = read(fd, raw, sizeof(raw) - 1); } while (count < 0 && errno == EINTR);
    }
    close(fd);
    if (count < 0 && st.st_size <= 64) return -1;
    std::string value = count >= 0 ? std::string(raw, static_cast<size_t>(count)) : std::string();
    for (volatile char& c : raw) c = 0;
    struct stat current;
    if (lstat(path.c_str(), &current) != 0 || current.st_dev != st.st_dev || current.st_ino != st.st_ino) {
        wipe(value); return -1;
    }
    if (unlink(path.c_str()) != 0) { wipe(value); return -1; }
    size_t start = 0, end = value.size();
    while (start < end && std::isspace(static_cast<unsigned char>(value[start]))) start++;
    while (end > start && std::isspace(static_cast<unsigned char>(value[end-1]))) end--;
    code = value.substr(start, end-start);
    wipe(value);
    if (!valid_code(code)) { wipe(code); return 0; }
    return 1;
}
inline std::string two_factor(const std::string& base_dir, bool from_file) {
    if (!from_file) {
        std::string code = hidden_line("2FA code: ", 6);
        if (!valid_code(code)) { wipe(code); throw Error("2FA code must contain six digits"); }
        return code;
    }
    int timeout = 60;
    const char* configured = getenv("WRAPPER_2FA_TIMEOUT_SECONDS");
    if (configured && *configured) {
        char* end = nullptr;
        long parsed = strtol(configured, &end, 10);
        if (end != configured && *end == 0 && parsed >= 1 && parsed <= 600) timeout = static_cast<int>(parsed);
    }
    fprintf(stderr, "Waiting for a private six-digit 2FA file in the mounted account directory...\n");
    for (int elapsed = 0; elapsed < timeout; ++elapsed) {
        std::string code;
        int result = read_code_file(base_dir + "/2fa.txt", code);
        if (result < 0) throw Error("unsafe 2FA file; use a regular non-symlink file with mode 600");
        if (result == 1) return code;
        sleep(1);
    }
    throw Error("2FA file timeout");
}
} // namespace login_input
