#pragma once
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
namespace private_cache {
inline std::string read(const std::string& path, size_t limit = 65536) {
    int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return "";
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || static_cast<size_t>(st.st_size) > limit) {
        close(fd); return "";
    }
    std::string value(static_cast<size_t>(st.st_size), '\0');
    size_t used = 0;
    while (used < value.size()) {
        ssize_t count = ::read(fd, &value[used], value.size() - used);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { close(fd); return ""; }
        used += static_cast<size_t>(count);
    }
    close(fd);
    if (value.find('\0') != std::string::npos) return "";
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) value.pop_back();
    return value;
}
inline bool write(const std::string& path, const std::string& content) {
    std::string pattern = path + ".tmp.XXXXXX";
    int fd = mkstemp(&pattern[0]);
    if (fd < 0) return false;
    size_t used = 0;
    bool ok = fchmod(fd, 0600) == 0;
    while (ok && used < content.size()) {
        ssize_t count = ::write(fd, content.data() + used, content.size() - used);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { ok = false; break; }
        used += static_cast<size_t>(count);
    }
    if (ok) ok = fsync(fd) == 0;
    if (close(fd) != 0) ok = false;
    if (ok) ok = rename(pattern.c_str(), path.c_str()) == 0;
    if (!ok) unlink(pattern.c_str());
    return ok;
}
}
