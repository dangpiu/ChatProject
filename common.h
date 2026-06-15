#ifndef NETWORK_COMMON_H
#define NETWORK_COMMON_H

// Conditional Preprocessor compilation to bridge Linux and Windows APIs
#ifdef _WIN32
    #define _WIN32_WINNT 0x0601 // Target Windows 7 and above
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib") // Link Winsock library dynamically
    
    typedef SOCKET SocketType;
    #define INVALID_SOCKET_VAL INVALID_SOCKET
    #define SOCKET_ERR SOCKET_ERROR
    #define CLOSE_SOCKET(s) closesocket(s)
#else
    #include <iostream>
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    #include <cstring>
    
    typedef int SocketType;
    #define INVALID_SOCKET_VAL -1
    #define SOCKET_ERR -1
    #define CLOSE_SOCKET(s) close(s)
#endif

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>

// Constants for Application Protocol
const int PORT = 8080;
const int BUFFER_SIZE = 4096;
const std::string FILE_TRANSFER_CMD = "/sendfile";

// Network Initialization abstraction
inline bool InitializeNetwork() {
#ifdef _WIN32
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
    return true; // Natively supported on Linux/POSIX
#endif
}

// Network Cleanup abstraction
inline void CleanUpNetwork() {
#ifdef _WIN32
    WSACleanup();
#endif
}

#endif
