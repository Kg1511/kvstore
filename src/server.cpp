#include "platform_net.hpp"
#include "net_io.hpp"
#include "protocol.hpp"
#include "kvstore.hpp"

#include <csignal>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <chrono>

KVStore* g_store = nullptr;
const std::string kSnapshotPath = "kvstore.snapshot";

std::vector<socket_t> g_replicas;
std::mutex g_replicas_mutex;

void HandleShutdown(int signal) {
    if (g_store) {
        std::cout << "\nsaving snapshot before exit...\n";
        g_store->SaveToFile(kSnapshotPath);
    }
    std::exit(0);
}

// Sends a write command to every currently-registered replica.
// Fire-and-forget: we don't wait for or check any response.
void ForwardToReplicas(uint8_t cmd_byte, const std::string& key, const std::string& value, bool has_value) {
    std::lock_guard<std::mutex> lock(g_replicas_mutex);
    for (auto it = g_replicas.begin(); it != g_replicas.end();) {
        socket_t fd = *it;
        bool ok = WriteExact(fd, &cmd_byte, sizeof(cmd_byte)) && WriteString(fd, key);
        if (ok && has_value) ok = WriteString(fd, value);

        if (!ok) {
            std::cout << "replica disconnected, removing\n";
            CLOSESOCKET(fd);
            it = g_replicas.erase(it);
        } else {
            ++it;
        }
    }
}
void ReplicaSyncLoop(const std::string& primary_host, int primary_port, KVStore* store) {
    while (true) {
        socket_t sock = socket(AF_INET, SOCK_STREAM, 0);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(primary_port));
        inet_pton(AF_INET, primary_host.c_str(), &addr.sin_addr);

        if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "could not connect to primary, retrying in 2s...\n";
            CLOSESOCKET(sock);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        uint8_t sync_cmd = static_cast<uint8_t>(Command::REPLICA_SYNC);
        WriteExact(sock, &sync_cmd, sizeof(sync_cmd));
        std::cout << "connected to primary at " << primary_host << ":" << primary_port << "\n";

        while (true) {
            uint8_t cmd_byte = 0;
            if (!ReadExact(sock, &cmd_byte, sizeof(cmd_byte))) break;

            std::string key;
            if (!ReadString(sock, &key)) break;

            switch (static_cast<Command>(cmd_byte)) {
                case Command::SET: {
                    std::string value;
                    if (!ReadString(sock, &value)) break;
                    store->Set(key, value);
                    std::cout << "[replica] SET " << key << "\n";
                    break;
                }
                case Command::DEL: {
                    store->Del(key);
                    std::cout << "[replica] DEL " << key << "\n";
                    break;
                }
                case Command::EXPIRE: {
                    std::string seconds_str;
                    if (!ReadString(sock, &seconds_str)) break;
                    store->Expire(key, std::stoi(seconds_str));
                    std::cout << "[replica] EXPIRE " << key << "\n";
                    break;
                }
                default:
                    break;
            }
        }

        std::cout << "disconnected from primary, retrying...\n";
        CLOSESOCKET(sock);
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

void HandleClient(socket_t client_fd, KVStore* store) {
    // First byte tells us if this is a normal client or a replica registering itself.
    uint8_t first_cmd = 0;
    if (!ReadExact(client_fd, &first_cmd, sizeof(first_cmd))) {
        CLOSESOCKET(client_fd);
        return;
    }

    if (static_cast<Command>(first_cmd) == Command::REPLICA_SYNC) {
        std::cout << "replica registered\n";
        {
            std::lock_guard<std::mutex> lock(g_replicas_mutex);
            g_replicas.push_back(client_fd);
        }
        // Keep this connection open; just watch for it closing.
        // We don't expect the replica to send anything further on this socket.
        char buf[1];
        while (recv(client_fd, buf, 1, 0) > 0) {}

        std::lock_guard<std::mutex> lock(g_replicas_mutex);
        auto it = std::find(g_replicas.begin(), g_replicas.end(), client_fd);
        if (it != g_replicas.end()) g_replicas.erase(it);
        CLOSESOCKET(client_fd);
        return;
    }

    // Normal client — process this first command, then loop for more.
    uint8_t cmd_byte = first_cmd;
    while (true) {
        std::string key;
        if (!ReadString(client_fd, &key)) break;

        switch (static_cast<Command>(cmd_byte)) {
            case Command::GET: {
                std::string value = store->Get(key);
                WriteString(client_fd, value);
                break;
            }
            case Command::SET: {
                std::string value;
                if (!ReadString(client_fd, &value)) { CLOSESOCKET(client_fd); return; }
                store->Set(key, value);
                WriteString(client_fd, "OK");
                ForwardToReplicas(cmd_byte, key, value, true);
                break;
            }
            case Command::DEL: {
                bool existed = store->Del(key);
                WriteString(client_fd, existed ? "1" : "0");
                ForwardToReplicas(cmd_byte, key, "", false);
                break;
            }
            case Command::EXPIRE: {
                std::string seconds_str;
                if (!ReadString(client_fd, &seconds_str)) { CLOSESOCKET(client_fd); return; }
                int seconds = std::stoi(seconds_str);
                bool ok = store->Expire(key, seconds);
                WriteString(client_fd, ok ? "1" : "0");
                ForwardToReplicas(cmd_byte, key, seconds_str, true);
                break;
            }
            default:
                CLOSESOCKET(client_fd);
                return;
        }

        if (!ReadExact(client_fd, &cmd_byte, sizeof(cmd_byte))) break;
    }
    CLOSESOCKET(client_fd);
}

int main(int argc, char* argv[]) {
    int port = argc > 1 ? std::stoi(argv[1]) : 6380;
    std::string replica_of_host;
    int replica_of_port = 0;
    bool is_replica = false;

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--replica-of" && i + 2 < argc) {
            replica_of_host = argv[i + 1];
            replica_of_port = std::stoi(argv[i + 2]);
            is_replica = true;
        }
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    socket_t server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "socket() failed\n";
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind() failed\n";
        return 1;
    }

    if (listen(server_fd, 16) < 0) {
        std::cerr << "listen() failed\n";
        return 1;
    }

    std::cout << "listening on port " << port << "\n";

    KVStore store;
    g_store = &store;
    store.LoadFromFile(kSnapshotPath);
    std::cout << "loaded snapshot (if any) from " << kSnapshotPath << "\n";

    std::signal(SIGINT, HandleShutdown);
    if (is_replica) {
        std::thread(ReplicaSyncLoop, replica_of_host, replica_of_port, &store).detach();
    }

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        socket_t client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            std::cerr << "accept() failed\n";
            continue;
        }

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        std::cout << "client connected from " << ip_str << "\n";

        std::thread(HandleClient, client_fd, &store).detach();
    }

    CLOSESOCKET(server_fd);

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}