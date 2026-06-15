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

bool clientRunning = true;

// Transmits file binary data chunks over the socket channel
void transmitFileStream(SocketType connectionSocket, const std::string& sourcePath) {
    std::ifstream binaryReader(sourcePath, std::ios::binary);
    if (!binaryReader.is_open()) {
        std::cerr << "[CLIENT-LOG] Failure: Unable to access target file profile: " << sourcePath << "\n";
        return;
    }

    std::string cleanName = sourcePath.substr(sourcePath.find_last_of("/\\") + 1);
    std::string handshakeHeader = FILE_TRANSFER_CMD + " " + cleanName;
    
    send(connectionSocket, handshakeHeader.c_str(), handshakeHeader.length(), 0);
    
#ifdef _WIN32
    Sleep(300); 
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
#endif

    char dataBlock[BUFFER_SIZE];
    std::cout << "[CLIENT-LOG] Processing binary stream transfer...\n";
    
    while (binaryReader.read(dataBlock, BUFFER_SIZE) || binaryReader.gcount() > 0) {
        send(connectionSocket, dataBlock, binaryReader.gcount(), 0);
    }
    binaryReader.close();

#ifdef _WIN32
    Sleep(300);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
#endif

    std::string boundaryToken = "##EOF##";
    send(connectionSocket, boundaryToken.c_str(), boundaryToken.length(), 0);
    std::cout << "[CLIENT-LOG] File synchronization complete.\n";
}

// Background thread loop designed to continuously catch text strings from the server
void backgroundReceiverLoop(SocketType connectionSocket) {
    char dataBuffer[BUFFER_SIZE];
    while (clientRunning) {
        ::memset(dataBuffer, 0, BUFFER_SIZE);
        int receivedBytes = recv(connectionSocket, dataBuffer, BUFFER_SIZE, 0);
        
        if (receivedBytes <= 0) {
            std::cout << "\n[CLIENT-LOG] Host dropped connection pipeline.\n";
            clientRunning = false;
            break;
        }
        
        std::cout << "\n[Server Response]: " << std::string(dataBuffer, receivedBytes) << "\n> " << std::flush;
    }
}

#ifdef _WIN32
DWORD WINAPI WinOSClientCallback(LPVOID executionParam) {
    SocketType* activeSocketRef = (SocketType*)executionParam;
    backgroundReceiverLoop(*activeSocketRef);
    return 0;
}
#endif

int main() {
    if (!InitializeNetwork()) return 1;

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

    if (connect(activeClientSocket, (sockaddr*)&socketConfig, sizeof(socketConfig)) == SOCKET_ERR) {
        std::cerr << "[ERROR] Remote handshake request rejected.\n";
        CLOSE_SOCKET(activeClientSocket);
        CleanUpNetwork();
        return 1;
    }

    std::cout << "====================================================\n";
    std::cout << "       CLIENT LINK SECURED (PIPELINE ESTABLISHED)   \n";
    std::cout << "====================================================\n";
    std::cout << ">> Commands: '/sendfile [path]' or type your text inputs directly.\n";

#ifdef _WIN32
    HANDLE nativeThreadDescriptor = CreateThread(NULL, 0, WinOSClientCallback, &activeClientSocket, 0, NULL);
    if (nativeThreadDescriptor != NULL) CloseHandle(nativeThreadDescriptor); 
#else
    std::thread independentRxThread(backgroundReceiverLoop, activeClientSocket);
    independentRxThread.detach();
#endif

    std::string userPromptString;
    while (clientRunning) {
        std::cout << "> ";
        std::getline(std::cin, userPromptString);
        
        if (userPromptString == "exit" || userPromptString == "quit") {
            clientRunning = false;
            break;
        }

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

    CLOSE_SOCKET(activeClientSocket);
    CleanUpNetwork();
    std::cout << "[CLIENT-LOG] Client context successfully destroyed.\n";
    return 0;
}
