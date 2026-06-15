#include <iostream>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

const int PORT = 8080;
const int BUFFER_SIZE = 1024;

// Function to handle receiving files
void receive_file(int client_socket, const std::string& filename, long long filesize) {
    std::ofstream outfile(filename, std::ios::binary);
    if (!outfile.is_open()) {
        std::cerr << "Error: Could not create file " << filename << std::endl;
        return;
    }

    char buffer[BUFFER_SIZE];
    long long total_bytes_received = 0;
    
    std::cout << "\n[System] Receiving file: " << filename << " (" << filesize << " bytes)..." << std::endl;

    while (total_bytes_received < filesize) {
        int bytes_to_read = std::min((long long)BUFFER_SIZE, filesize - total_bytes_received);
        int bytes_received = recv(client_socket, buffer, bytes_to_read, 0);
        if (bytes_received <= 0) {
            std::cerr << "Connection lost during file transfer!" << std::endl;
            break;
        }
        outfile.write(buffer, bytes_received);
        total_bytes_received += bytes_received;
    }
    
    outfile.close();
    std::cout << "[System] File transfer complete! Saved as: " << filename << "\nChat: " << std::flush;
}

// Thread function dedicated to constantly listening for incoming data from Windows
void receive_messages(int client_socket) {
    char buffer[BUFFER_SIZE];
    while (true) {
        std::memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
        
        if (bytes_received <= 0) {
            std::cout << "\n[System] Client disconnected." << std::endl;
            break;
        }

        std::string incoming_data(buffer, bytes_received);

        // Check if the data is a file header protocol trigger
        if (incoming_data.rfind("FILE_HEADER:", 0) == 0) {
            // Protocol parsing: FILE_HEADER:[filename]:[filesize]
            size_t first_colon = incoming_data.find(':', 12);
            std::string filename = incoming_data.substr(12, first_colon - 12);
            long long filesize = std::stoll(incoming_data.substr(first_colon + 1));
            
            // Switch to file receiving mode
            receive_file(client_socket, filename, filesize);
        } else {
            // It's a normal chat text message
            std::cout << "\n[Windows Client]: " << incoming_data << "\nServer: " << std::flush;
        }
    }
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed!" << std::endl;
        return 1;
    }

    if (listen(server_fd, 3) < 0) {
        std::cerr << "Listen failed!" << std::endl;
        return 1;
    }

    std::cout << "Server listening on port " << PORT << "..." << std::endl;

    int addrlen = sizeof(address);
    int client_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
    if (client_socket < 0) {
        std::cerr << "Accept failed!" << std::endl;
        return 1;
    }

    std::cout << "Connected to Windows Client! Type your messages below." << std::endl;
    std::cout << "To send a file, type: /sendfile [path_to_file]" << std::endl;

    // Start background thread to handle incoming chat/files
    std::thread rx_thread(receive_messages, client_socket);
    rx_thread.detach();

    // Main thread handles outgoing typing input
    std::string out_message;
    while (true) {
        std::cout << "Server: ";
        std::getline(std::cin, out_message);
        if (out_message.empty()) continue;

        if (out_message.rfind("/sendfile ", 0) == 0) {
            std::string filepath = out_message.substr(10);
            std::ifstream infile(filepath, std::ios::binary | std::ios::ate);
            if (!infile.is_open()) {
                std::cerr << "File does not exist!" << std::endl;
                continue;
            }
            long long filesize = infile.tellg();
            infile.seekg(0, std::ios::beg);

            // Extract file name from path
            std::string filename = filepath.substr(filepath.find_last_of("/\\") + 1);

            // Send metadata header protocol
            std::string header = "FILE_HEADER:" + filename + ":" + std::to_string(filesize);
            send(client_socket, header.c_str(), header.length(), 0);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Short pause for synchronization

            // Stream file binary packets
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

    close(client_socket);
    close(server_fd);
    return 0;
}
