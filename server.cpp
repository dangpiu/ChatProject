#include <iostream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>

// Tell the compiler to link the Winsock library (works in MSVC)
#pragma comment(lib, "ws2_32.lib")

const int PORT = 8080;
const char* SERVER_IP = "127.0.0.1"; // We will change this to your Ubuntu IP later!

int main() {
    // 1. Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Winsock initialization failed!\n";
        return 1;
    }

    // 2. Create Socket
    SOCKET client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed!\n";
        WSACleanup();
        return 1;
    }

    // 3. Define Server Address
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    std::cout << "Connecting to server...\n";

    // 4. Connect to Server
    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Connection failed!\n";
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "Connected successfully!\n";

    // 5. Send a Message
    std::string message = "Hello from Windows Client!";
    send(client_socket, message.c_str(), message.length(), 0);
    std::cout << "Message sent!\n";

    // 6. Cleanup
    closesocket(client_socket);
    WSACleanup();
    return 0;
}
