#define _WIN32_WINNT 0x0601

#include "network_common.h"
#include <chrono>
#include <cstring>
#include <string.h>

// Handle platform-specific threading headers without std::thread dependencies on Windows
#ifdef _WIN32
    #include <windows.h>
#else
    #include <thread>
#endif

// Transmits file binary data chunks over the socket channel
void transmitFileStream(SocketType connectionSocket, const std::string& sourcePath) {
    std::ifstream binaryReader(sourcePath, std::ios::binary);
    if (!binaryReader.is_open()) {
        std::cerr << "[NODE-LOG] Failure: Unable to access target file profile: " << sourcePath << "\n";
        return;
    }

    // Extract file name component
    std::string cleanName = sourcePath.substr(sourcePath.find_last_of("/\\") + 1);
    std::string handshakeHeader = FILE_TRANSFER_CMD + " " + cleanName;
    
    // Broadcast intent payload
    send(connectionSocket, handshakeHeader.c_str(), handshakeHeader.length(), 0);
    
    // Native platform timing adjustments to prevent packet collision
#ifdef _WIN32
    Sleep(300); 
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
#endif

    char dataBlock[BUFFER_SIZE];
    std::cout << "[NODE-LOG] Processing binary stream transfer...\n";
    
    while (binaryReader.read(dataBlock, BUFFER_SIZE) || binaryReader.gcount() > 0) {
        send(connectionSocket, dataBlock, binaryReader.gcount(), 0);
    }
    binaryReader.close();

#ifdef _WIN32
    Sleep(300);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
#endif

    // Flush terminating signature string
    std::string boundaryToken = "##EOF##";
    send(connectionSocket, boundaryToken.c_str(), boundaryToken.length(), 0);
    std::cout << "[NODE-LOG] File synchronization complete.\n";
}

// Background thread loop designed to continuously catch data incoming from host
void backgroundReceiverLoop(SocketType connectionSocket) {
    char dataBuffer[BUFFER_SIZE];
    while (true) {
        ::memset(dataBuffer, 0, BUFFER_SIZE);
        int receivedBytes = recv(connectionSocket, dataBuffer, BUFFER_SIZE, 0);
        
        if (receivedBytes <= 0) {
            std::cout << "\n[NODE-LOG] Host dropped connection pipeline.\n";
            break;
        }
        
        // Print message payload cleanly
        std::cout << "\n[Incoming Message]: " << std::string(dataBuffer, receivedBytes) << "\n> " << std::flush;
    }
}

// Win32 System Thread Context Handshake Redirector
#ifdef _WIN32
DWORD WINAPI WinOSThreadCallback(LPVOID executionParam) {
    SocketType* activeSocketRef = (SocketType*)executionParam;
    backgroundReceiverLoop(*activeSocketRef);
    return 0;
}
#endif

int main() {
    if (!InitializeNetwork()) {
        std::cerr << "[CRITICAL] Network subsystem initialization failed.\n";
        return 1;
    }

    SocketType activeClientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (activeClientSocket == INVALID_SOCKET_VAL) {
        CleanUpNetwork();
        return 1;
    }

    std::string serverIPInput;
    std::cout << "Target Remote Node Address Configuration IP: ";
    std::getline(std::cin, serverIPInput);

    sockaddr_in socketConfig{};
    socketConfig.sin_family = AF_INET;
    socketConfig.sin_port = htons(PORT);
    
#ifdef _WIN32
    socketConfig.sin_addr.s_addr = inet_addr(serverIPInput.c_str());
#else
    inet_pton(AF_INET, serverIPInput.c_str(), &socketConfig.sin_addr);
#endif

    // Attempt channel hook interface connection
    if (connect(activeClientSocket, (sockaddr*)&socketConfig, sizeof(socketConfig)) == SOCKET_ERR) {
        std::cerr << "[ERROR] Remote handshake request rejected.\n";
        CLOSE_SOCKET(activeClientSocket);
        CleanUpNetwork();
        return 1;
    }

    std::cout << ">> Pipeline Active! Commands: '/sendfile [path]' or send text inputs.\n";

    // Dynamic execution pathway selection for background collection tasks
#ifdef _WIN32
    HANDLE nativeThreadDescriptor = CreateThread(NULL, 0, WinOSThreadCallback, &activeClientSocket, 0, NULL);
    if (nativeThreadDescriptor != NULL) {
        CloseHandle(nativeThreadDescriptor); 
    }
#else
    std::thread independentRxThread(backgroundReceiverLoop, activeClientSocket);
    independentRxThread.detach();
#endif

    std::string userPromptString;
    while (true) {
        std::cout << "> ";
        std::getline(std::cin, userPromptString);
        
        if (userPromptString == "exit" || userPromptString == "quit") {
            break;
        }

        // Intercept transfer directive commands
        if (userPromptString.rfind(FILE_TRANSFER_CMD, 0) == 0) {
            size_t commandPrefixLength = FILE_TRANSFER_CMD.length();
            
            if (userPromptString.length() > commandPrefixLength + 1) {
                std::string structuralPath = userPromptString.substr(commandPrefixLength + 1);
                transmitFileStream(activeClientSocket, structuralPath);
            } else {
                std::cout << "[WARN] Parameter missing. Syntax: /sendfile <file_path>\n";
            }
        } else if (!userPromptString.empty()) {
            send(activeClientSocket, userPromptString.c_str(), userPromptString.length(), 0);
        }
    }

    // Clean resource extraction destruction sequence
    CLOSE_SOCKET(activeClientSocket);
    CleanUpNetwork();
    std::cout << "[NODE-LOG] Client context successfully destroyed.\n";
    return 0;
}
