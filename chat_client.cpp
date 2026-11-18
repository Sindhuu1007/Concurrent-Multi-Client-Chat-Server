#include "common.hpp"
#include <iostream>
#include <netdb.h>
#include <vector>
#include <sstream>
#include <cstring>

struct Config {
    std::string disc_host = "127.0.0.1";
    int disc_port = 5050;
    std::string chat_host = "127.0.0.1";
    int chat_port = 6060;
};

static int connect_tcp(const std::string &host, int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "socket() failed: " << std::strerror(errno) << std::endl;
        return -1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_aton(host.c_str(), &addr.sin_addr) == 0) {
        hostent *he = gethostbyname(host.c_str());
        if (!he) { std::cerr << "DNS failed for host '" << host << "'" << std::endl; close(fd); return -1; }
        addr.sin_addr = *(in_addr*)he->h_addr_list[0];
    }
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "connect failed to " << host << ":" << port << ": " << std::strerror(errno) << std::endl;
        close(fd);
        return -1;
    }
    return fd;
}

static bool discovery_register(const Config &cfg, const std::string &user, const std::string &pass, int chat_port) {
    int fd = connect_tcp(cfg.disc_host, cfg.disc_port);
    if (fd < 0) return false;
    send_line(fd, "REGISTER " + user + " " + pass + " " + std::to_string(chat_port));
    std::string line; bool ok = recv_line_blocking(fd, line) && line.rfind("OK", 0) == 0;
    close(fd);
    return ok;
}

static bool discovery_lookup(const Config &cfg, const std::string &user) {
    int fd = connect_tcp(cfg.disc_host, cfg.disc_port);
    if (fd < 0) return false;
    send_line(fd, "LOOKUP " + user);
    std::string line; bool ok = recv_line_blocking(fd, line);
    if (ok) std::cout << line << std::endl; else std::cout << "Lookup failed" << std::endl;
    close(fd);
    return ok;
}

// Resolve a user's advertised endpoint via discovery; returns false on any parse/network failure.
static bool discovery_lookup_endpoint(const Config &cfg, const std::string &user, std::string &ip, int &port) {
    int fd = connect_tcp(cfg.disc_host, cfg.disc_port);
    if (fd < 0) return false;
    send_line(fd, "LOOKUP " + user);
    std::string line;
    bool ok = recv_line_blocking(fd, line) && line.rfind("OK ", 0) == 0;
    close(fd);
    if (!ok) return false;
    std::istringstream iss(line.substr(3));
    if (!(iss >> ip >> port)) return false;
    return true;
}

static std::string prompt_line(const std::string &label, const std::string &def = "") {
    std::cout << label;
    std::string s; std::getline(std::cin, s);
    if (s.empty()) return def;
    return s;
}

static void chat_session(int fd, const std::string &username) {
    std::cout << "Chat mode\nCommands: /all <text> | /msg <user> <text> | /who | /logout | /quit | /help" << std::endl;
    fd_set rfds;
    std::string inbuf;
    while (true) {
        FD_ZERO(&rfds);
        FD_SET(0, &rfds);
        FD_SET(fd, &rfds);
        int maxfd = fd;
        if (select(maxfd + 1, &rfds, nullptr, nullptr, nullptr) < 0) break;
        if (FD_ISSET(fd, &rfds)) {
            char tmp[512];
            ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
            if (n <= 0) { std::cout << "Server closed" << std::endl; break; }
            inbuf.append(tmp, tmp + n);
            while (true) {
                auto pos = inbuf.find('\n');
                if (pos == std::string::npos) break;
                std::string l = trim(inbuf.substr(0, pos));
                inbuf.erase(0, pos + 1);
                if (!l.empty()) std::cout << l << std::endl;
            }
        }
        if (FD_ISSET(0, &rfds)) {
            std::string input;
            if (!std::getline(std::cin, input)) break;
            if (input.rfind("/all ", 0) == 0 || input.rfind("BROADCAST ", 0) == 0) {
                std::string payload = input.rfind("/all ", 0) == 0 ? input.substr(5) : input.substr(10);
                send_line(fd, "BCAST " + payload);
            } else if (input.rfind("/msg ", 0) == 0 || input.rfind("PRIVATE ", 0) == 0) {
                std::string rest = input.rfind("/msg ", 0) == 0 ? input.substr(5) : input.substr(8);
                auto pos = rest.find(' ');
                if (pos == std::string::npos) { std::cout << "Usage: /msg <user> <text>" << std::endl; continue; }
                std::string target = rest.substr(0, pos);
                std::string text = rest.substr(pos + 1);
                send_line(fd, "MSG " + target + " " + text);
            } else if (input == "/who" || input == "ACTIVE") {
                send_line(fd, "WHO");
            } else if (input == "/logout" || input == "Bye") {
                send_line(fd, "QUIT");
                std::string line; recv_line_blocking(fd, line);
                std::cout << line << std::endl;
                break;
            } else if (input == "/quit") {
                send_line(fd, "QUIT");
                std::string line; recv_line_blocking(fd, line);
                std::cout << line << std::endl;
                exit(0);
            } else if (input == "/help") {
                std::cout << "Commands: /all <text> | /msg <user> <text> | /who | /logout | /quit" << std::endl;
            } else {
                std::cout << "Commands: /all <text> | /msg <user> <text> | /who | /logout | /quit" << std::endl;
            }
        }
    }
}

int main(int argc, char **argv) {
    Config cfg;
    if (argc == 7) {
        cfg.disc_host = argv[1];
        cfg.disc_port = std::stoi(argv[2]);
        cfg.chat_host = argv[3];
        cfg.chat_port = std::stoi(argv[4]);
    } else {
        // Use defaults: 127.0.0.1:5050 for discovery, 127.0.0.1:6060 for chat.
        std::cout << "Interactive mode (using discovery 127.0.0.1:5050, chat 127.0.0.1:6060).\n";
    }

    while (true) {
        std::cout << "\nMenu:\n";
        std::cout << "1) Register 2) Login (via discovery) 3) Lookup user 4) Quit \n Choose 1-4: ";
        std::string choice; if (!std::getline(std::cin, choice)) break;
        if (choice == "1") {
            std::string user = prompt_line("Username: ");
            std::string pass = prompt_line("Password: ");
            int port = cfg.chat_port; // all clients share the same chat server port
            if (discovery_register(cfg, user, pass, port)) std::cout << "Registered" << std::endl;
            else std::cout << "Register failed" << std::endl;
        } else if (choice == "2") {
            std::string user, pass;
            if (argc == 7) {
                user = argv[5]; pass = argv[6];
            } else {
                user = prompt_line("Username: ");
                pass = prompt_line("Password: ");
            }
            std::string ip = cfg.chat_host;
            int port = cfg.chat_port;
            if (!discovery_lookup_endpoint(cfg, user, ip, port)) {
                std::cout << "Discovery lookup failed; falling back to " << ip << ":" << port << std::endl;
            }
            int fd = connect_tcp(ip, port);
            if (fd < 0) { std::cout << "Chat connect failed" << std::endl; continue; }
            send_line(fd, "AUTH " + user + " " + pass);
            std::string line;
            if (!recv_line_blocking(fd, line) || line.rfind("OK", 0) != 0) {
                std::cout << "Auth failed: " << line << std::endl;
                close(fd);
                continue;
            }
            std::cout << "Logged in as " << user << " at " << ip << ":" << port << std::endl;
            chat_session(fd, user);
            close(fd);
        } else if (choice == "3") {
            std::string user = prompt_line("Lookup username: ");
            discovery_lookup(cfg, user);
        } else if (choice == "4" || choice == "q" || choice == "quit") {
            break;
        } else {
            std::cout << "Pick 1-4" << std::endl;
        }
    }
    return 0;
}
