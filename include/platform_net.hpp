#pragma once

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    using socket_t = SOCKET;
    #define CLOSESOCKET closesocket
#else
    #include <arpa/inet.h>
    #include <sys/socket.h>
    #include <unistd.h>
    using socket_t = int;
    #define CLOSESOCKET close
#endif