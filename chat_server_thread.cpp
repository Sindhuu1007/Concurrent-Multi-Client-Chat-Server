#include "common.hpp"
#include <unordered_map>
#include <fstream>
#include <thread>
#include <mutex>
#include <iostream>

struct Cred { std::string password; };
struct ClientInfo { int fd; std::string username; };

static std::unordered_map<std::string, Cred> credentials;
static std::unordered_map<std::string, ClientInfo> clients;
static std::mutex clients_mu;
static std::mutex creds_mu;
static const char *DB_FILE = "discovery_db.txt";

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
    reload_creds_locked(); // pull in new registrations each AUTH
    auto it = credentials.find(user);
    return it != credentials.end() && it->second.password == pass;
}

static void broadcast_from(const std::string &sender, const std::string &text) {
    std::lock_guard<std::mutex> lk(clients_mu);
    for (auto &kv : clients) {
        if (kv.first == sender) continue;
        send_line(kv.second.fd, "BCAST " + sender + " " + text);
    }
}

static bool send_private(const std::string &sender, const std::string &target, const std::string &text) {
    std::lock_guard<std::mutex> lk(clients_mu);
    auto it = clients.find(target);
    if (it == clients.end()) return false;
    send_line(it->second.fd, "FROM " + sender + " " + text);
    return true;
}

static std::string user_list() {
    std::lock_guard<std::mutex> lk(clients_mu);
    if (clients.empty()) return "No";
    std::string out;
    bool first = true;
    for (auto &kv : clients) {
        if (!first) out.push_back(',');
        out += kv.first;
        first = false;
    }
    return out;
}

static void remove_user(const std::string &user) {
    std::lock_guard<std::mutex> lk(clients_mu);
    clients.erase(user);
}

static void handle_client(int fd) {
    std::string line;
    if (!recv_line_blocking(fd, line)) { close(fd); return; }
    auto auth = split_first(line);
    if (auth.first != "AUTH") { send_line(fd, "ERR expected-AUTH"); close(fd); return; }
    auto rest = split_first(auth.second);
    std::string user = rest.first;
    std::string pass = rest.second;
    if (!auth_ok(user, pass)) { send_line(fd, "ERR auth-failed"); close(fd); return; }
    {
        std::lock_guard<std::mutex> lk(clients_mu);
        if (clients.count(user)) { send_line(fd, "ERR already-logged-in"); close(fd); return; }
        clients[user] = {fd, user};
    }
    send_line(fd, "OK");

    while (recv_line_blocking(fd, line)) {
        if (line.empty()) continue;
        auto p = split_first(line);
        std::string cmd = p.first;
        if (cmd == "BCAST") {
            broadcast_from(user, p.second);
            send_line(fd, "OK");
        } else if (cmd == "MSG") {
            auto q = split_first(p.second);
            std::string target = q.first;
            std::string text = q.second;
            if (send_private(user, target, text)) send_line(fd, "OK");
            else send_line(fd, "ERR no-such-user");
        } else if (cmd == "WHO") {
            send_line(fd, "OK " + user_list());
        } else if (cmd == "QUIT") {
            send_line(fd, "OK");
            break;
        } else {
            send_line(fd, "ERR bad-command");
        }
    }
    close(fd);
    remove_user(user);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: chat_server_thread <port>" << std::endl;
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
    if (listen(srv, 64) < 0) { perror("listen"); return 1; }

    std::cout << "Chat server (thread) listening on " << port << std::endl;

    while (true) {
        sockaddr_in peer{};
        socklen_t len = sizeof(peer);
        int fd = accept(srv, (sockaddr*)&peer, &len);
        if (fd < 0) { perror("accept"); continue; }
        std::thread(handle_client, fd).detach();
    }
}
