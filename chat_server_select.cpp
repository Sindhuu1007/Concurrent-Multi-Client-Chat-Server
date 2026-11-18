#include "common.hpp"
#include <unordered_map>
#include <fstream>
#include <mutex>
#include <vector>
#include <iostream>
#include <fcntl.h>

// Select-based server with a hard cap of 10 concurrent clients.

struct Cred { std::string password; };
struct Client {
    int fd = -1;
    bool authed = false;
    std::string user;
    std::string buf; // partial line buffer
};

static std::unordered_map<std::string, Cred> credentials;
static std::mutex creds_mu;
static const char *DB_FILE = "discovery_db.txt";
static const int MAX_CLIENTS = 10;

static void reload_creds_locked() {
    credentials.clear();
    std::ifstream in(DB_FILE);
    std::string user, pass, ip;
    int port;
    while (in >> user >> pass >> ip >> port) {
        credentials[user] = {pass};
    }
}

static bool auth_ok(const std::string &user, const std::string &pass) {
    std::lock_guard<std::mutex> lk(creds_mu);
    reload_creds_locked();
    auto it = credentials.find(user);
    return it != credentials.end() && it->second.password == pass;
}

static std::string user_list(const std::vector<Client> &clients) {
    bool any = false;
    std::string out;
    for (auto &c : clients) {
        if (!c.authed) continue;
        if (any) out.push_back(',');
        out += c.user;
        any = true;
    }
    if (!any) return "No";
    return out;
}

static void broadcast_from(const std::vector<Client> &clients, const std::string &sender, const std::string &text) {
    for (auto &c : clients) {
        if (!c.authed || c.user == sender) continue;
        send_line(c.fd, "BCAST " + sender + " " + text);
    }
}

static bool send_private(const std::vector<Client> &clients, const std::string &sender, const std::string &target, const std::string &text) {
    for (auto &c : clients) {
        if (c.authed && c.user == target) {
            send_line(c.fd, "FROM " + sender + " " + text);
            return true;
        }
    }
    return false;
}

static void set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void close_client(Client &c) {
    if (c.fd >= 0) close(c.fd);
    c = Client{};
}

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: chat_server_select <port>" << std::endl;
        return 1;
    }
    int port = std::stoi(argv[1]);
    {
        std::lock_guard<std::mutex> lk(creds_mu);
        reload_creds_locked();
    }

    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(srv, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(srv, MAX_CLIENTS) < 0) { perror("listen"); return 1; }
    set_nonblock(srv);

    std::cout << "Chat server (select) listening on " << port << " (max " << MAX_CLIENTS << " clients)" << std::endl;

    std::vector<Client> clients(MAX_CLIENTS);

    while (true) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(srv, &rfds);
        int maxfd = srv;
        for (auto &c : clients) {
            if (c.fd >= 0) {
                FD_SET(c.fd, &rfds);
                if (c.fd > maxfd) maxfd = c.fd;
            }
        }

        int ret = select(maxfd + 1, &rfds, nullptr, nullptr, nullptr);
        if (ret < 0) {
            perror("select");
            break;
        }

        if (FD_ISSET(srv, &rfds)) {
            sockaddr_in peer{};
            socklen_t len = sizeof(peer);
            int fd = accept(srv, (sockaddr*)&peer, &len);
            if (fd >= 0) {
                set_nonblock(fd);
                bool placed = false;
                for (auto &c : clients) {
                    if (c.fd < 0) {
                        c.fd = fd;
                        placed = true;
                        break;
                    }
                }
                if (!placed) {
                    send_line(fd, "ERR server-full");
                    close(fd);
                }
            }
        }

        for (auto &c : clients) {
            if (c.fd < 0) continue;
            if (!FD_ISSET(c.fd, &rfds)) continue;
            char buf[512];
            ssize_t n = recv(c.fd, buf, sizeof(buf), 0);
            if (n <= 0) { close_client(c); continue; }
            c.buf.append(buf, buf + n);
            while (true) {
                auto pos = c.buf.find('\n');
                if (pos == std::string::npos) break;
                std::string line = trim(c.buf.substr(0, pos));
                c.buf.erase(0, pos + 1);
                if (line.empty()) continue;

                if (!c.authed) {
                    auto auth = split_first(line);
                    if (auth.first != "AUTH") { send_line(c.fd, "ERR expected-AUTH"); close_client(c); break; }
                    auto rest = split_first(auth.second);
                    std::string user = rest.first;
                    std::string pass = rest.second;
                    if (!auth_ok(user, pass)) { send_line(c.fd, "ERR auth-failed"); close_client(c); break; }
                    // ensure user not already logged in
                    bool dup = false;
                    for (auto &other : clients) {
                        if (&other != &c && other.authed && other.user == user) { dup = true; break; }
                    }
                    if (dup) { send_line(c.fd, "ERR already-logged-in"); close_client(c); break; }
                    c.authed = true; c.user = user;
                    send_line(c.fd, "OK");
                    continue;
                }

                auto p = split_first(line);
                std::string cmd = p.first;
                if (cmd == "BCAST") {
                    broadcast_from(clients, c.user, p.second);
                    send_line(c.fd, "OK");
                } else if (cmd == "MSG") {
                    auto q = split_first(p.second);
                    std::string target = q.first;
                    std::string text = q.second;
                    if (send_private(clients, c.user, target, text)) send_line(c.fd, "OK");
                    else send_line(c.fd, "ERR no-such-user");
                } else if (cmd == "WHO") {
                    send_line(c.fd, "OK " + user_list(clients));
                } else if (cmd == "QUIT") {
                    send_line(c.fd, "OK");
                    close_client(c);
                    break;
                } else {
                    send_line(c.fd, "ERR bad-command");
                }
            }
        }
    }

    close(srv);
    return 0;
}
