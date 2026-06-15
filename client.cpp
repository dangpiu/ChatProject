#include <iostream>
#include <string>
#include <cstring>

#ifdef _WIN32
    // Windows Specific Headers & Libraries [cite: 45]
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    // Ubuntu / Linux Specific Headers [cite: 45]
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    typedef int SOCKET;
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket close
#endif

const int PORT = 8080;
// We use localhost (127.0.0.1) for testing if both apps run on the same system.
// If your server is on a separate Ubuntu machine, change this to your Ubuntu IP!
const char* SERVER_IP = "127.0.0.1"; 

int main() {
    // 1. Initialize Network Architecture (Only required for Windows) [cite: 45]
    #ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "Winsock initialization failed!\n";
            return 1;
        }
    #endif

    // 2. Create the Client Socket [cite: 24]
    SOCKET client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed!\n";
        #ifdef _WIN32
            WSACleanup();
        #endif
        return 1;
    }

    // 3. Define Server Address Setup
    sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    
    // Convert IP address from text to binary format
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address / Address not supported!\n";
        closesocket(client_socket);
        #ifdef _WIN32
            WSACleanup();
        #endif
        return 1;
    }

    std::cout << "Connecting to server at " << SERVER_IP << ":" << PORT << "...\n";

    // 4. Connect to the Remote Server
    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Connection to server failed!\n";
        closesocket(client_socket);
        #ifdef _WIN32
            WSACleanup();
        #endif
        return 1;
    }

    std::cout << "Connected successfully to the server!\n";

    // 5. Send an initial message string to the Server
    std::string message = "Hello from your Cross-Platform Client!";
    int bytesSent = send(client_socket, message.c_str(), message.length(), 0);
    if (bytesSent == SOCKET_ERROR) {
        std::cerr << "Failed to send message!\n";
    } else {
        std::cout << "Greeting sent to server!\n";
    }

    // 6. Clean up and exit cleanly [cite: 45]
    closesocket(client_socket);
    #ifdef _WIN32
        WSACleanup(); // Clean up Winsock allocations [cite: 45]
    #endif

    std::cout << "Client disconnected cleanly.\n";
    return 0;
}
