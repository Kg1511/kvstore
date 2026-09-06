#include "platform_net.hpp"
#include "net_io.hpp"
#include "protocol.hpp"
#include "kvstore.hpp"

#include <cstdint>
#include <iostream>
#include <thread>
#include <csignal>

KVStore* g_store = nullptr;
const std::string kSnapshotPath = "kvstore.snapshot";

void HandleShutdown(int signal) {
    if (g_store) {
        std::cout << "\nsaving snapshot before exit...\n";
        g_store->SaveToFile(kSnapshotPath);
    }
    std::exit(0);
}

void HandleClient(socket_t client_fd, KVStore* store) {
    while (true) {
        uint8_t cmd_byte = 0;
        if (!ReadExact(client_fd, &cmd_byte, sizeof(cmd_byte))) break;

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
                break;
            }
            case Command::DEL: {
                bool existed = store->Del(key);
                WriteString(client_fd, existed ? "1" : "0");
                break;
            }
            case Command::EXPIRE: {
                std::string seconds_str;
                if (!ReadString(client_fd, &seconds_str)) { CLOSESOCKET(client_fd); return; }
                int seconds = std::stoi(seconds_str);
                bool ok = store->Expire(key, seconds);
                WriteString(client_fd, ok ? "1" : "0");
                break;
            }
            default:
                CLOSESOCKET(client_fd);
                return;
        }
    }
    CLOSESOCKET(client_fd);
}

int main(int argc, char* argv[]) {
    int port = argc > 1 ? std::stoi(argv[1]) : 6380;

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