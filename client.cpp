// =====================================================================
//  client.cpp  -  Cross-platform chat client (Linux <-> Windows)
// =====================================================================
//  Compile on Linux:
//      g++ -std=c++17 -pthread client.cpp -o client
//
//  Compile on Windows (MinGW-w64 g++):
//      g++ -std=c++17 client.cpp -o client.exe -lws2_32
//
//  Run (replace with the SERVER's IP address):
//      ./client 192.168.1.10        (Linux)
//      client.exe 192.168.1.10      (Windows)
//
//  Once connected, both sides can chat in real time and send files
//  using:
//      /sendfile <path>     -> send a file to the other side
//      /quit                -> end the chat session
// =====================================================================

#include "common.h"

bool running = true;   // shared flag used to stop both threads cleanly

void receiveLoop(SOCKET sock);
void sendLoop(SOCKET sock);

int main(int argc, char* argv[]) {
    // ---- Winsock initialisation (no-op on Linux) ----
    if (!initWinsock()) return 1;

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <server_ip_address>\n";
        cleanupWinsock();
        return 1;
    }
    std::string serverIP = argv[1];

    // -----------------------------------------------------------------
    // 1) socket() - create an endpoint for communication (TCP/IPv4).
    // -----------------------------------------------------------------
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "[!] socket() failed\n";
        cleanupWinsock();
        return 1;
    }

    // -----------------------------------------------------------------
    // Build the server's address structure.
    //    htons()    converts the port number to network byte order.
    //    inet_pton() converts a dotted-decimal IP string (e.g.
    //                "192.168.1.10") into the binary form required
    //                by the socket API. Works identically on Linux
    //                and Windows.
    // -----------------------------------------------------------------
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port   = htons(PORT);

    if (inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "[!] Invalid server IP address: " << serverIP << "\n";
        closesocket(sock);
        cleanupWinsock();
        return 1;
    }

    // -----------------------------------------------------------------
    // 2) connect() - actively establish a TCP connection to the server
    //    identified by serverAddr (the 3-way handshake happens here).
    // -----------------------------------------------------------------
    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "[!] connect() failed - is the server running and reachable?\n";
        closesocket(sock);
        cleanupWinsock();
        return 1;
    }

    std::cout << "[+] Connected to server " << serverIP << ":" << PORT << "\n";
    std::cout << "Type a message and press Enter to chat.\n";
    std::cout << "Use /sendfile <path> to send a file, or /quit to end the chat.\n\n";

    // -----------------------------------------------------------------
    // Run sending and receiving on separate threads so messages/files
    // can arrive WHILE the user is typing (full-duplex, real-time chat).
    // -----------------------------------------------------------------
    std::thread tRecv(receiveLoop, sock);
    std::thread tSend(sendLoop, sock);

    tRecv.join();
    tSend.join();

    // -----------------------------------------------------------------
    // 3) closesocket()/close() - release the socket resources.
    // -----------------------------------------------------------------
    closesocket(sock);
    cleanupWinsock();
    return 0;
}

// =====================================================================
// receiveLoop()
// Continuously waits for framed messages from the server and either:
//   - prints chat text, or
//   - receives and saves an incoming file, or
//   - stops the program if the server sent /quit or disconnected.
// =====================================================================
void receiveLoop(SOCKET sock) {
    while (running) {
        uint8_t type;
        if (!recvAll(sock, (char*)&type, 1)) {
            std::cout << "\n[!] Connection closed by peer.\n";
            running = false;
            break;
        }

        if (type == MSG_TEXT) {
            uint32_t lenNet;
            if (!recvAll(sock, (char*)&lenNet, 4)) { running = false; break; }
            uint32_t len = ntohl(lenNet);

            std::string text(len, '\0');
            if (len > 0 && !recvAll(sock, &text[0], len)) { running = false; break; }

            std::cout << "\n[Server]: " << text << "\nYou: ";
            std::cout.flush();
        }
        else if (type == MSG_FILE) {
            // ---- read filename ----
            uint32_t nameLenNet;
            if (!recvAll(sock, (char*)&nameLenNet, 4)) { running = false; break; }
            uint32_t nameLen = ntohl(nameLenNet);

            std::string filename(nameLen, '\0');
            if (nameLen > 0 && !recvAll(sock, &filename[0], nameLen)) { running = false; break; }

            // ---- read file size ----
            uint32_t sizeNet;
            if (!recvAll(sock, (char*)&sizeNet, 4)) { running = false; break; }
            uint32_t fileSize = ntohl(sizeNet);

            std::cout << "\n[*] Receiving file '" << filename << "' (" << fileSize << " bytes)...\n";

            // ---- read file data and write to disk ----
            std::string outName = "received_" + filename;
            std::ofstream outFile(outName, std::ios::binary);
            char buffer[BUFFER_SIZE];
            uint32_t remaining = fileSize;
            while (remaining > 0) {
                size_t chunk = (remaining > (uint32_t)BUFFER_SIZE) ? (size_t)BUFFER_SIZE : remaining;
                if (!recvAll(sock, buffer, chunk)) { running = false; break; }
                outFile.write(buffer, chunk);
                remaining -= (uint32_t)chunk;
            }
            outFile.close();

            std::cout << "[+] File saved as '" << outName << "'\nYou: ";
            std::cout.flush();
        }
        else if (type == MSG_QUIT) {
            std::cout << "\n[!] Server ended the chat session.\n";
            running = false;
            break;
        }
    }
}

// =====================================================================
// sendLoop()
// Reads lines typed by the client's user and sends them to the server.
//   - "/sendfile <path>" triggers a file transfer
//   - "/quit"            ends the session for both sides
//   - anything else      is sent as a plain chat message
// =====================================================================
void sendLoop(SOCKET sock) {
    std::string line;
    while (running) {
        std::cout << "You: ";
        if (!std::getline(std::cin, line)) {   // e.g. Ctrl+D / Ctrl+Z
            sendQuit(sock);
            running = false;
            break;
        }

        if (!running) break;

        if (line == "/quit") {
            sendQuit(sock);
            running = false;
        }
        else if (line.rfind("/sendfile ", 0) == 0) {
            std::string path = line.substr(10); // text after "/sendfile "
            sendFileMsg(sock, path);
        }
        else if (!line.empty()) {
            if (!sendText(sock, line)) running = false;
        }
    }
}
