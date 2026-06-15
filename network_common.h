#ifndef NETWORK_COMMON_H
#define NETWORK_COMMON_H

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

// --- CROSS-PLATFORM ARCHITECTURE DEFINITIONS ---
#if defined(_WIN32) || defined(WIN32)
    #define SYSTEM_WINDOWS 1
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    
    typedef SOCKET SocketType;
    #define CLOSE_SOCKET(s) closesocket(s)
    #define INVALID_SOCKET_VAL INVALID_SOCKET
    #define SOCKET_ERR SOCKET_ERROR
#else
    #define SYSTEM_WINDOWS 0
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    
    typedef int SocketType;
    #define CLOSE_SOCKET(s) close(s)
    #define INVALID_SOCKET_VAL -1
    #define SOCKET_ERR -1
#endif

// --- SYSTEM GLOBAL PARAMETERS ---
const int PORT = 8080;
const int BUFFER_SIZE = 4096;
const std::string FILE_TRANSFER_CMD = "/sendfile";

// --- ENVIRONMENT MANAGEMENT INTERFACES ---
inline bool InitializeNetwork() {
#if SYSTEM_WINDOWS
    WSADATA wsaRuntimeData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaRuntimeData) != 0) {
        std::cerr << "[CRITICAL] Initialization of Winsock subsystem failed.\n";
        return false;
    }
#endif
    return true;
}

inline void CleanUpNetwork() {
#if SYSTEM_WINDOWS
    WSACleanup();
#endif
}

#endif // NETWORK_COMMON_H
