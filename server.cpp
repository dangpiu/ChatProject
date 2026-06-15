// =====================================================================
//  server.cpp  -  Cross-platform chat server (Linux <-> Windows)
// =====================================================================
//  Compile on Linux:
//      g++ -std=c++17 -pthread server.cpp -o server
//
//  Compile on Windows (MinGW-w64 g++):
//      g++ -std=c++17 server.cpp -o server.exe -lws2_32
//
//  Run:
//      ./server            (Linux)
//      server.exe          (Windows)
//
//  The server listens on TCP port 5500, accepts ONE client, then both
//  sides can chat in real time and send files using:
//      /sendfile <path>     -> send a file to the other side
//      /quit                -> end the chat session
// =====================================================================

#include "common.h"

bool running = true;   // shared flag used to stop both threads cleanly

void receiveLoop(SOCKET sock);
void sendLoop(SOCKET sock);

int main() {
    // ---- Winsock initialisation (no-op on Linux) ----
    if (!initWinsock()) return 1;

    // -----------------------------------------------------------------
    // 1) socket() - create an endpoint for communication.
    //    AF_INET     = IPv4
    //    SOCK_STREAM = TCP (reliable, connection-oriented byte stream)
    // -----------------------------------------------------------------
    SOCKET serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock == INVALID_SOCKET) {
        std::cerr << "[!] socket() failed" << std::endl;
        cleanupWinsock();
        return 1;
    }

    // Allow the OS to immediately re-use this port if the program is
    // restarted (avoids "Address already in use" errors during testing)
    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    // -----------------------------------------------------------------
    // 2) bind() - assign a local IP address and port number to the socket.
    //    INADDR_ANY  = accept connections on any of this machine's
    //                  network interfaces (e.g. Ethernet, Wi-Fi, VM NAT).
    //    htons()     = convert port number to network byte order.
    // -----------------------------------------------------------------
    sockaddr_in serverAddr{};
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port        = htons(PORT);

    if (bind(serverSock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "[!] bind() failed - is port " << PORT << " already in use?" << std::endl;
        closesocket(serverSock);
        cleanupWinsock();
        return 1;
    }

    // -----------------------------------------------------------------
    // 3) listen() - mark the socket as PASSIVE: it will be used to
    //    accept incoming connection requests. The second argument (1)
    //    is the maximum length of the queue of pending connections.
    // -----------------------------------------------------------------
    if (listen(serverSock, 1) == SOCKET_ERROR) {
        std::cerr << "[!] listen() failed" << std::endl;
        closesocket(serverSock);
        cleanupWinsock();
        return 1;
    }

    std::cout << "[*] Server started. Waiting for a client on port " << PORT << "...\n";

    // -----------------------------------------------------------------
    // 4) accept() - blocks until a client connects, then returns a NEW
    //    socket descriptor (clientSock) dedicated to that client.
    //    The original serverSock keeps listening (not used further
    //    here since this demo handles a single client).
    // -----------------------------------------------------------------
    sockaddr_in clientAddr{};
    socklen_t   clientLen = sizeof(clientAddr);
    SOCKET clientSock = accept(serverSock, (sockaddr*)&clientAddr, &clientLen);
    if (clientSock == INVALID_SOCKET) {
        std::cerr << "[!] accept() failed" << std::endl;
        closesocket(serverSock);
        cleanupWinsock();
        return 1;
    }

    // inet_ntop() converts the client's binary IP address into a
    // human-readable string (e.g. "192.168.1.10")
    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
    std::cout << "[+] Client connected from " << ipStr << ":" << ntohs(clientAddr.sin_port) << "\n";
    std::cout << "Type a message and press Enter to chat.\n";
    std::cout << "Use /sendfile <path> to send a file, or /quit to end the chat.\n\n";

    // -----------------------------------------------------------------
    // Run sending and receiving on separate threads so that messages
    // (or files) can be received WHILE the user is typing - this is
    // what makes the chat "real-time" / full-duplex.
    // -----------------------------------------------------------------
    std::thread tRecv(receiveLoop, clientSock);
    std::thread tSend(sendLoop, clientSock);

    tRecv.join();
    tSend.join();

    // -----------------------------------------------------------------
    // 5) closesocket()/close() - release the socket resources.
    // -----------------------------------------------------------------
    closesocket(clientSock);
    closesocket(serverSock);
    cleanupWinsock();
    return 0;
}

// =====================================================================
// receiveLoop()
// Continuously waits for framed messages from the client and either:
//   - prints chat text, or
//   - receives and saves an incoming file, or
//   - stops the program if the client sent /quit or disconnected.
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

            std::cout << "\n[Client]: " << text << "\nYou: ";
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
            std::cout << "\n[!] Client ended the chat session.\n";
            running = false;
            break;
        }
    }
}

// =====================================================================
// sendLoop()
// Reads lines typed by the server's user and sends them to the client.
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
