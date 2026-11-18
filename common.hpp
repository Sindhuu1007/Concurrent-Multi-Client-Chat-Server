#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static const size_t MAX_LINE_LEN = 4096;

inline std::string trim(const std::string &s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) b++;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
    return s.substr(b, e - b);
}

inline std::pair<std::string, std::string> split_first(const std::string &s) {
    auto pos = s.find(' ');
    if (pos == std::string::npos) return {s, ""};
    return {s.substr(0, pos), s.substr(pos + 1)};
}

inline bool send_line(int fd, const std::string &line) {
    std::string out = line;
    out.push_back('\n');
    const char *buf = out.c_str();
    size_t left = out.size();
    while (left > 0) {
        ssize_t n = ::send(fd, buf, left, 0);
        if (n <= 0) return false;
        buf += n;
        left -= static_cast<size_t>(n);
    }
    return true;
}

inline bool recv_line_blocking(int fd, std::string &line_out) {
    line_out.clear();
    char buf[256];
    while (line_out.size() < MAX_LINE_LEN) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        for (ssize_t i = 0; i < n; ++i) {
            char c = buf[i];
            if (c == '\n') {
                line_out = trim(line_out);
                return true;
            }
            line_out.push_back(c);
        }
    }
    return false;
}

inline std::vector<std::string> split_args(const std::string &s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}
