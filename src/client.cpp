#include "platform_net.hpp"
#include "net_io.hpp"
#include "protocol.hpp"

#include <cstdint>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: kvstore_client <SET|GET|DEL> <key> [value]\n";
        return 1;
    }

    std::string op = argv[1];
    std::string key = argv[2];

#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    int port = argc > 4 ? std::stoi(argv[4]) : 6380;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "connect() failed — is the server running?\n";
        return 1;
    }

    uint8_t cmd_byte;
    if (op == "SET") {
        cmd_byte = static_cast<uint8_t>(Command::SET);
    } else if (op == "GET") {
        cmd_byte = static_cast<uint8_t>(Command::GET);
    } else if (op == "DEL") {
        cmd_byte = static_cast<uint8_t>(Command::DEL);
    }else if (op == "EXPIRE") {
        cmd_byte = static_cast<uint8_t>(Command::EXPIRE); 
    } else {
        std::cerr << "Unknown op: " << op << "\n";
        return 1;
    }

    WriteExact(sock, &cmd_byte, sizeof(cmd_byte));
    WriteString(sock, key);
    if (op == "SET" || op == "EXPIRE") {
        if (argc < 4) {
            std::cerr << op << " requires a third argument\n";
            return 1;
        }
        WriteString(sock, argv[3]);
    }

    std::string response;
    if (ReadString(sock, &response)) {
        std::cout << response << "\n";
    } else {
        std::cerr << "no response / connection closed\n";
    }

    CLOSESOCKET(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}