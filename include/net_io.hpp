#pragma once

#include "platform_net.hpp"

#include <cstdint>
#include <string>

inline bool ReadExact(socket_t fd, void* buf, size_t n) {
    size_t total = 0;
    while (total < n) {
        int r = recv(fd, static_cast<char*>(buf) + total, static_cast<int>(n - total), 0);
        if (r <= 0) return false;
        total += static_cast<size_t>(r);
    }
    return true;
}

inline bool WriteExact(socket_t fd, const void* buf, size_t n) {
    size_t total = 0;
    while (total < n) {
        int w = send(fd, static_cast<const char*>(buf) + total, static_cast<int>(n - total), 0);
        if (w <= 0) return false;
        total += static_cast<size_t>(w);
    }
    return true;
}

inline bool ReadString(socket_t fd, std::string* out) {
    uint32_t len = 0;
    if (!ReadExact(fd, &len, sizeof(len))) return false;
    len = ntohl(len);
    out->resize(len);
    return len == 0 || ReadExact(fd, out->data(), len);
}

inline bool WriteString(socket_t fd, const std::string& s) {
    uint32_t len = htonl(static_cast<uint32_t>(s.size()));
    if (!WriteExact(fd, &len, sizeof(len))) return false;
    return s.empty() || WriteExact(fd, s.data(), s.size());
}