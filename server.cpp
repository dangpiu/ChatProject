#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <thread>
#include <vector>
#include <algorithm>

// --- PREPROCESSOR CONDITIONAL COMPILATION ---
#if defined(_WIN32) || defined(WIN32)
    #define PLATFORM_WINDOWS 1
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET SocketType;
    #define CLOSE_SOCKET(s) closesocket(s)
    #define INVALID_SOCKET_VAL INVALID_SOCKET
#else
    #define PLATFORM_WINDOWS 0
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    typedef int SocketType;
    #define CLOSE_SOCKET(s) close(s)
    #define INVALID_SOCKET_VAL -1
#endif

#define PORT 8080
#define BUFFER_SIZE 4096
#define FILE_MARKER "__FILE_TRANSFER__:"

bool running = true;

// --- UTILITY FUNCTIONS ---
void initialize_network() {
#if PLATFORM_WINDOWS
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[-] Winsock initialization failed.\n";
        exit(EXIT_FAILURE);
    }
#endif
}

void cleanup_network() {
#if PLATFORM_WINDOWS
    WSACleanup();
#endif
}

// --- FILE TRANSFER LOGIC ---
void send_file(SocketType sock, const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[-] Error: Cannot open file " << filepath << "\n";
        return;
    }

    std::streamsize filesize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string filename = filepath.substr(filepath.find_last_of("/\\") + 1);
    std::string header = std::string(FILE_MARKER) + filename + "|" + std::to_string(filesize);
    send(sock, header.c_str(), header.length(), 0);

    // Short delay to avoid packet bleeding into raw data stream
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::vector<char> buffer(BUFFER_SIZE);
    std::cout << "[+] Sending file: " << filename << " (" << filesize << " bytes)...\n";
    
    while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0) {
        send(sock, buffer.data(), file.gcount(), 0);
    }
    
    file.close();
    std::cout << "[+] File sent successfully!\n";
}

void receive_file(SocketType sock, const std::string& header_info) {
    size_t delimiter = header_info.find('|');
    if (delimiter == std::string::npos) return;

    std::string filename = "server_received_" + header_info.substr(0, delimiter);
    long long filesize = std::stoll(header_info.substr(delimiter + 1));

    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[-] Failed to create local file: " << filename << "\n";
        return;
    }

    std::cout << "\n[+] Receiving file: " << filename << " [" << filesize << " bytes]\n";
    std::vector<char> buffer(BUFFER_SIZE);
    long long total_received = 0;

    while (total_received < filesize) {
        int bytes_to_read = std::min((long long)BUFFER_SIZE, filesize - total_received);
        int bytes_received = recv(sock, buffer.data(), bytes_to_read, 0);
        if (bytes_received <= 0) break;

        file.write(buffer.data(), bytes_received);
        total_received += bytes_received;
    }

    file.close();
    std::cout << "[+] File saved successfully as: " << filename << "\n>> " << std::flush;
}

// --- BACKGROUND BACKGROUND RECEIVER ---
void handle_receiving(SocketType sock) {
    std::vector<char> buffer(BUFFER_SIZE);
    while (running) {
        std::memset(buffer.data(), 0, BUFFER_SIZE);
        int bytes_received = recv(sock, buffer.data(), BUFFER_SIZE - 1, 0);

        if (bytes_received <= 0) {
            std::cout << "\n[-] Client disconnected.\n";
            running = false;
            break;
        }

        std::string msg(buffer.data(), bytes_received);
        
        if (msg.rfind(FILE_MARKER, 0) == 0) {
            std::string header_info = msg.substr(std::strlen(FILE_MARKER));
            receive_file(sock, header_info);
        } else {
            std::cout << "\nClient: " << msg << "\n>> " << std::flush;
        }
    }
}

// --- MAIN SERVER EXECUTION ---
int main() {
    initialize_network();

    std::cout << "====================================================\n";
    std::cout << "        SERVER NODE (CROSS-PLATFORM HOST)          \n";
    std::cout << "====================================================\n";

    SocketType server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET_VAL) {
        std::cerr << "[-] Socket creation failed.\n";
        cleanup_network();
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "[-] Bind failed.\n";
        CLOSE_SOCKET(server_fd);
        cleanup_network();
        return 1;
    }

    if (listen(server_fd, 3) < 0) {
        std::cerr << "[-] Listen failed.\n";
        CLOSE_SOCKET(server_fd);
        cleanup_network();
        return 1;
    }

    std::cout << "[+] Server listening on port " << PORT << ". Waiting for client...\n";
    
    sockaddr_in client_addr{};
    int addrlen = sizeof(client_addr);
    SocketType client_sock = accept(server_fd, (struct sockaddr*)&client_addr, (socklen_t*)&addrlen);
    
    if (client_sock == INVALID_SOCKET_VAL) {
        std::cerr << "[-] Accept connection failed.\n";
        CLOSE_SOCKET(server_fd);
        cleanup_network();
        return 1;
    }
    std::cout << "[+] Client connected successfully!\n\n";
    std::cout << "* Type messages normally and press Enter.\n";
    std::cout << "* Use '/sendfile <path>' to transfer files.\n";
    std::cout << "* Type 'exit' to quit.\n----------------------------------------------------\n";

    std::thread rx_thread(handle_receiving, client_sock);

    std::string input;
    while (running) {
        std::cout << ">> ";
        std::getline(std::cin, input);
        if (input == "exit") { running = false; break; }

        if (input.rfind("/sendfile ", 0) == 0) {
            std::string path = input.substr(10);
            send_file(client_sock, path);
        } else if (!input.empty()) {
            send(client_sock, input.c_str(), input.length(), 0);
        }
    }

    if (rx_thread.joinable()) rx_thread.join();
    
    CLOSE_SOCKET(client_sock);
    CLOSE_SOCKET(server_fd);
    cleanup_network();
    std::cout << "[+] Server stopped cleanly.\n";
    return 0;
}
