#pragma once
#include <string>
namespace request_validation {
inline bool adam_id(const std::string& value) {
    if (value.empty() || value.size() > 32) return false;
    for (unsigned char c : value) if (c < '0' || c > '9') return false;
    return true;
}
inline bool key_uri(const std::string& value) {
    if (value.size() <= 6 || value.size() > 240 || value.compare(0, 6, "skd://") != 0) return false;
    for (unsigned char c : value) if (c <= 32 || c >= 127) return false;
    return true;
}
}
