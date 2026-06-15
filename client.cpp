#define _WIN32_WINNT 0x0600
#include <iostream>
#include <string>
#include <cstring>
#include <fstream>
#include <thread>
#include <chrono>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

const int PORT = 8080;
const int BUFFER_SIZE = 1024;
const char* SERVER_IP = "127.0.0.1"; // Change to your exact Ubuntu IP address!

void receive_file(SOCKET server_socket, const std::string& filename, long long filesize) {
    std::ofstream outfile(filename, std::ios::binary);
    if (!outfile.is_open()) {
        std::cerr << "Error: Could not open output file file." << std::endl;
        return;
    }

    char buffer[BUFFER_SIZE];
    long long total_bytes_received = 0;
    std::cout << "\n[System] Receiving file: " << filename << " (" << filesize << " bytes)..." << std::endl;

    while (total_bytes_received < filesize) {
        int bytes_to_read = std::min((long long)BUFFER_SIZE, filesize - total_bytes_received);
        int bytes_received = recv(server_socket, buffer, bytes_to_read, 0);
        if (bytes_received <= 0) {
            std::cerr << "Connection interrupted!" << std::endl;
            break;
        }
        outfile.write(buffer, bytes_received);
        total_bytes_received += bytes_received;
    }
    outfile.close();
    std::cout << "[System] File transfer complete!\nClient: " << std::flush;
}

void receive_messages(SOCKET server_socket) {
    char buffer[BUFFER_SIZE];
    while (true) {
        std::memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(server_socket, buffer, BUFFER_SIZE, 0);
        if (bytes_received <= 0) {
            std::cout << "\n[System] Connection lost from server." << std::endl;
            break;
        }

        std::string incoming_data(buffer, bytes_received);

        if (incoming_data.rfind("FILE_HEADER:", 0) == 0) {
            size_t first_colon = incoming_data.find(':', 12);
            std::string filename = incoming_data.substr(12, first_colon - 12);
            long long filesize = std::stoll(incoming_data.substr(first_colon + 1));
            
            receive_file(server_socket, filename, filesize);
        } else {
            std::cout << "\n[Ubuntu Server]: " << incoming_data << "\nClient: " << std::flush;
        }
    }
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;

    SOCKET client_socket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    std::cout << "Connecting to server..." << std::endl;
    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Connection failed!" << std::endl;
        WSACleanup();
        return 1;
    }

    std::cout << "Connected to Ubuntu Server! Start chatting." << std::endl;
    std::cout << "To send a file, type: /sendfile [path_to_file]" << std::endl;

    std::thread rx_thread(receive_messages, client_socket);
    rx_thread.detach();

    std::string out_message;
    while (true) {
        std::cout << "Client: ";
        std::getline(std::cin, out_message);
        if (out_message.empty()) continue;

        if (out_message.rfind("/sendfile ", 0) == 0) {
            std::string filepath = out_message.substr(10);
            std::ifstream infile(filepath, std::ios::binary | std::ios::ate);
            if (!infile.is_open()) {
                std::cerr << "File could not be opened!" << std::endl;
                continue;
            }
            long long filesize = infile.tellg();
            infile.seekg(0, std::ios::beg);

            std::string filename = filepath.substr(filepath.find_last_of("/\\") + 1);
            std::string header = "FILE_HEADER:" + filename + ":" + std::to_string(filesize);
            send(client_socket, header.c_str(), header.length(), 0);

            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            char file_buffer[BUFFER_SIZE];
            while (!infile.eof()) {
                infile.read(file_buffer, BUFFER_SIZE);
                send(client_socket, file_buffer, infile.gcount(), 0);
            }
            infile.close();
            std::cout << "[System] File sent successfully!" << std::endl;
        } else {
            send(client_socket, out_message.c_str(), out_message.length(), 0);
        }
    }

    closesocket(client_socket);
    WSACleanup();
    return 0;
}
