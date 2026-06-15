#define _WIN32_WINNT 0x0601

#include "network_common.h"
#include <chrono>
#include <cstring>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <thread>
#endif

bool serverRunning = true;

// Low-level processing stream designed to ingest a file until the boundary token arrives
void ingestIncomingFile(SocketType clientSocket, const std::string& commandHeader) {
    std::string fileName = commandHeader;
    std::string safeLocalName = "server_received_" + fileName;

    std::ofstream diskWriter(safeLocalName, std::ios::binary);
    if (!diskWriter.is_open()) {
        std::cerr << "[SERVER-LOG] Failure: Local storage file creation denied: " << safeLocalName << "\n";
        return;
    }

    std::cout << "\n[SERVER-LOG] Opening binary file stream for inbound data: " << safeLocalName << "\n";
    char rawDataBuffer[BUFFER_SIZE];
    std::string streamingCache = "";

    while (serverRunning) {
        ::memset(rawDataBuffer, 0, BUFFER_SIZE);
        int receivedChunk = recv(clientSocket, rawDataBuffer, BUFFER_SIZE, 0);
        
        if (receivedChunk <= 0) {
            std::cerr << "[SERVER-LOG] Transfer interrupted. Connection dropped.\n";
            break;
        }

        std::string evaluatingBlock(rawDataBuffer, receivedChunk);
        size_t boundaryIndex = evaluatingBlock.find("##EOF##");

        if (boundaryIndex != std::string::npos) {
            // Write everything prior to the boundary token
            diskWriter.write(evaluatingBlock.c_str(), boundaryIndex);
            std::cout << "[SERVER-LOG] Inbound file transfer completed successfully.\n> " << std::flush;
            break;
        } else {
            diskWriter.write(rawDataBuffer, receivedChunk);
        }
    }
    diskWriter.close();
}

// Thread pipeline loop that continuously polls for incoming textual packets or commands
void primaryReceiverLoop(SocketType clientSocket) {
    char communicationBuffer[BUFFER_SIZE];
    while (serverRunning) {
        ::memset(communicationBuffer, 0, BUFFER_SIZE);
        int capturedBytes = recv(clientSocket, communicationBuffer, BUFFER_SIZE - 1, 0);

        if (capturedBytes <= 0) {
            std::cout << "\n[SERVER-LOG] Remote endpoint dropped out of the session.\n";
            serverRunning = false;
            break;
        }

        std::string interpretedString(communicationBuffer, capturedBytes);
        size_t commandPrefixLength = FILE_TRANSFER_CMD.length();

        // Check if the received packet is a file initialization request
        if (interpretedString.rfind(FILE_TRANSFER_CMD, 0) == 0) {
            std::string fileNameArgument = interpretedString.substr(commandPrefixLength + 1);
            ingestIncomingFile(clientSocket, fileNameArgument);
        } else {
            std::cout << "\n[Client Node]: " << interpretedString << "\n> " << std::flush;
        }
    }
}

#ifdef _WIN32
DWORD WINAPI WinOSServerCallback(LPVOID macroParam) {
    SocketType* activeClientRef = (SocketType*)macroParam;
    primaryReceiverLoop(*activeClientRef);
    return 0;
}
#endif

int main() {
    if (!InitializeNetwork()) return 1;

    SocketType hostingServerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (hostingServerSocket == INVALID_SOCKET_VAL) {
        CleanUpNetwork();
        return 1;
    }

    sockaddr_in serverAddressConfig{};
    serverAddressConfig.sin_family = AF_INET;
    serverAddressConfig.sin_port = htons(PORT);
    serverAddressConfig.sin_addr.s_addr = INADDR_ANY;

    if (bind(hostingServerSocket, (sockaddr*)&serverAddressConfig, sizeof(serverAddressConfig)) == SOCKET_ERR) {
        std::cerr << "[CRITICAL] Interface binding sequence failed.\n";
        CLOSE_SOCKET(hostingServerSocket);
        CleanUpNetwork();
        return 1;
    }

    if (listen(hostingServerSocket, 2) == SOCKET_ERR) {
        std::cerr << "[CRITICAL] Passive backlog listener initialization failed.\n";
        CLOSE_SOCKET(hostingServerSocket);
        CleanUpNetwork();
        return 1;
    }

    std::cout << "====================================================\n";
    std::cout << "       SERVER ACTIVE (AWAITING INBOUND CONNECTIONS) \n";
    std::cout << "====================================================\n";
    std::cout << "[*] Listening on interface port: " << PORT << "...\n";

    sockaddr_in clientIdentityFallback{};
    int identityLengthSize = sizeof(clientIdentityFallback);
    SocketType activeSessionSocket = accept(hostingServerSocket, (sockaddr*)&clientIdentityFallback, (socklen_t*)&identityLengthSize);

    if (activeSessionSocket == INVALID_SOCKET_VAL) {
        std::cerr << "[ERROR] Session pipeline instantiation failure.\n";
        CLOSE_SOCKET(hostingServerSocket);
        CleanUpNetwork();
        return 1;
    }

    std::cout << "[+] Secured endpoint hook validation! Connection initialized.\n";

#ifdef _WIN32
    HANDLE systemThreadHandle = CreateThread(NULL, 0, WinOSServerCallback, &activeSessionSocket, 0, NULL);
    if (systemThreadHandle != NULL) CloseHandle(systemThreadHandle);
#else
    std::thread unixWorkerStream(primaryReceiverLoop, activeSessionSocket);
    unixWorkerStream.detach();
#endif

    std::string localTerminalInput;
    while (serverRunning) {
        std::cout << "> ";
        std::getline(std::cin, localTerminalInput);

        if (localTerminalInput == "exit" || localTerminalInput == "quit") {
            serverRunning = false;
            break;
        }

        if (!localTerminalInput.empty()) {
            send(activeSessionSocket, localTerminalInput.c_str(), localTerminalInput.length(), 0);
        }
    }

    CLOSE_SOCKET(activeSessionSocket);
    CLOSE_SOCKET(hostingServerSocket);
    CleanUpNetwork();
    std::cout << "[SERVER-LOG] Execution terminated gracefully.\n";
    return 0;
}
