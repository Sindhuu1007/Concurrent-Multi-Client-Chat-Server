#include "common.hpp"
#include <unordered_map>
#include <fstream>
#include <thread>
#include <mutex>
#include <iostream>

struct UserRecord {
    std::string password;
    std::string ip;
    int port = 0;
};

static std::unordered_map<std::string, UserRecord> db;
static std::mutex db_mu;
static const char *DB_FILE = "discovery_db.txt";

static void load_db() {
    std::ifstream in(DB_FILE);
    if (!in) return;
    std::string user, pass, ip;
    int port;
    while (in >> user >> pass >> ip >> port) {
        db[user] = {pass, ip, port};
    }
}

static void save_db() {
    std::ofstream out(DB_FILE, std::ios::trunc);
    for (auto &kv : db) {
        out << kv.first << ' ' << kv.second.password << ' ' << kv.second.ip << ' ' << kv.second.port << '\n';
    }
}

static void handle_client(int fd, sockaddr_in peer) {
    std::string line;
    while (recv_line_blocking(fd, line)) {
        auto parts = split_args(line);
        if (parts.empty()) continue;
        const std::string &cmd = parts[0];
        if (cmd == "REGISTER" && parts.size() == 4) {
            std::string user = parts[1];
            std::string pass = parts[2];
            int port = std::stoi(parts[3]);
            std::string ip = inet_ntoa(peer.sin_addr);
            std::lock_guard<std::mutex> lk(db_mu);
            auto it = db.find(user);
            if (it != db.end() && it->second.password != pass) {
                send_line(fd, "ERR wrong-password");
                continue;
            }
            db[user] = {pass, ip, port};
            save_db();
            send_line(fd, "OK");
        } else if (cmd == "LOOKUP" && parts.size() == 2) {
            std::string user = parts[1];
            std::lock_guard<std::mutex> lk(db_mu);
            auto it = db.find(user);
            if (it == db.end()) {
                send_line(fd, "ERR not-found");
            } else {
                send_line(fd, "OK " + it->second.ip + " " + std::to_string(it->second.port));
            }
        } else {
            send_line(fd, "ERR bad-command");
        }
    }
    close(fd);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: discovery_server <port>" << std::endl;
        return 1;
    }
    int port = std::stoi(argv[1]);
    load_db();

    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(srv, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(srv, 32) < 0) { perror("listen"); return 1; }

    std::cout << "Discovery server listening on port " << port << std::endl;

    while (true) {
        sockaddr_in peer{};
        socklen_t len = sizeof(peer);
        int fd = accept(srv, (sockaddr*)&peer, &len);
        if (fd < 0) { perror("accept"); continue; }
        std::thread(handle_client, fd, peer).detach();
    }
}
