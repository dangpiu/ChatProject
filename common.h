#ifndef COMMON_H
#define COMMON_H

#include <iostream>
#include <string>
#include <thread>
#include <fstream>
#include <cstring>
#include <cstdint>

// ===================================================================
//  CROSS-PLATFORM SOCKET HEADERS
// -------------------------------------------------------------------
//  Linux (Berkeley Sockets)            Windows (Winsock2)
//  ----------------------------        ----------------------------
//  <sys/socket.h>, <arpa/inet.h>       <winsock2.h>, <ws2tcpip.h>
//  close()                             closesocket()
//  No init/cleanup required            WSAStartup() / WSACleanup()
//
//  The #ifdef _WIN32 block below selects the correct headers and
//  defines small macros (closesocket, SOCKET, INVALID_SOCKET,
//  SOCKET_ERROR) so the REST of the program can be written ONCE and
//  compiled unmodified on both operating systems.
// ===================================================================
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")   // tell MSVC to link Winsock library
    typedef int socklen_t;
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define closesocket close
    typedef int SOCKET;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR   (-1)
#endif

const int PORT        = 5500;   // TCP port used by both server and client
const int BUFFER_SIZE = 4096;   // chunk size used when streaming file data

// -------------------------------------------------------------------
// Application-layer message type identifiers (1 byte each).
// These let the receiver know how to interpret the bytes that follow.
// -------------------------------------------------------------------
const uint8_t MSG_TEXT = 1;   // a normal chat message
const uint8_t MSG_FILE = 2;   // a file transfer
const uint8_t MSG_QUIT = 3;   // the sender is ending the chat session

// ===================================================================
//  initWinsock() / cleanupWinsock()
// -------------------------------------------------------------------
//  Winsock requires WSAStartup() to be called once before ANY socket
//  function is used, and WSACleanup() once the program is done.
//  Berkeley sockets on Linux need none of this, so on Linux these
//  functions simply do nothing (the preprocessor removes the body).
// ===================================================================
inline bool initWinsock() {
#ifdef _WIN32
    WSADATA wsaData;
    int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (res != 0) {
        std::cerr << "WSAStartup failed with error: " << res << std::endl;
        return false;
    }
#endif
    return true;
}

inline void cleanupWinsock() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// ===================================================================
//  sendAll() / recvAll()
// -------------------------------------------------------------------
//  TCP is a STREAM protocol. A single send() may transmit fewer
//  bytes than requested, and a single recv() may return fewer bytes
//  than the sender actually sent (it can also split one send() across
//  several recv() calls). These two helpers loop until exactly `len`
//  bytes have been transferred, which our framed protocol below
//  depends on.
// ===================================================================
inline bool sendAll(SOCKET sock, const char* data, size_t len) {
    size_t totalSent = 0;
    while (totalSent < len) {
        int sent = send(sock, data + totalSent, (int)(len - totalSent), 0);
        if (sent <= 0) return false;          // connection error/closed
        totalSent += (size_t)sent;
    }
    return true;
}

inline bool recvAll(SOCKET sock, char* buffer, size_t len) {
    size_t totalReceived = 0;
    while (totalReceived < len) {
        int received = recv(sock, buffer + totalReceived, (int)(len - totalReceived), 0);
        if (received <= 0) return false;      // connection error/closed
        totalReceived += (size_t)received;
    }
    return true;
}

// ===================================================================
//  APPLICATION-LAYER PROTOCOL (FRAMING)
// -------------------------------------------------------------------
//  Every message sent over the socket has this layout:
//
//    [1 byte  : type]
//
//    if type == MSG_TEXT
//        [4 bytes : text length, network byte order]
//        [N bytes : UTF-8 text]
//
//    if type == MSG_FILE
//        [4 bytes : filename length, network byte order]
//        [N bytes : filename]
//        [4 bytes : file size,     network byte order]
//        [N bytes : raw file data]
//
//    if type == MSG_QUIT
//        (no payload)
//
//  Prefixing every message with its type and length solves the
//  classic TCP "message boundary" problem: the receiver always knows
//  exactly how many bytes belong to the current message, even though
//  TCP itself has no concept of "messages".
//
//  htonl()/ntohl() convert 32-bit values between HOST byte order
//  (which differs between machines/architectures) and NETWORK byte
//  order (big-endian), which is the standard for protocols.
// ===================================================================

// Send a chat text message
inline bool sendText(SOCKET sock, const std::string& text) {
    uint8_t  type = MSG_TEXT;
    uint32_t len  = htonl((uint32_t)text.size());

    if (!sendAll(sock, (char*)&type, 1)) return false;
    if (!sendAll(sock, (char*)&len, 4))  return false;
    return sendAll(sock, text.c_str(), text.size());
}

// Tell the other side we are ending the chat
inline bool sendQuit(SOCKET sock) {
    uint8_t type = MSG_QUIT;
    return sendAll(sock, (char*)&type, 1);
}

// Send a file (reads from disk, sends header + raw bytes in chunks)
inline bool sendFileMsg(SOCKET sock, const std::string& path) {
    std::ifstream inFile(path, std::ios::binary | std::ios::ate);
    if (!inFile) {
        std::cerr << "[!] Cannot open file: " << path << std::endl;
        return false;
    }

    std::streamoff size = inFile.tellg();   // file size in bytes
    inFile.seekg(0, std::ios::beg);

    // Strip any directory part so only the file name itself is sent,
    // e.g. "/home/user/notes.txt" -> "notes.txt"
    std::string filename = path;
    size_t pos = filename.find_last_of("/\\");
    if (pos != std::string::npos) filename = filename.substr(pos + 1);

    uint8_t  type     = MSG_FILE;
    uint32_t nameLen  = htonl((uint32_t)filename.size());
    uint32_t fileSize = htonl((uint32_t)size);

    if (!sendAll(sock, (char*)&type, 1))               return false;
    if (!sendAll(sock, (char*)&nameLen, 4))            return false;
    if (!sendAll(sock, filename.c_str(), filename.size())) return false;
    if (!sendAll(sock, (char*)&fileSize, 4))           return false;

    // Stream the file contents in BUFFER_SIZE chunks
    char buffer[BUFFER_SIZE];
    std::streamoff remaining = size;
    while (remaining > 0) {
        std::streamsize chunk = (remaining > BUFFER_SIZE) ? BUFFER_SIZE
                                                           : (std::streamsize)remaining;
        inFile.read(buffer, chunk);
        if (!sendAll(sock, buffer, (size_t)chunk)) return false;
        remaining -= chunk;
    }

    std::cout << "[+] Sent file '" << filename << "' (" << size << " bytes)\n";
    return true;
}

#endif // COMMON_H
