#include <atomic>
#include <iostream>
#include <winsock2.h>
#include <vector>
#include <thread>
#include <chrono>

using namespace std;

struct DataHeader {
    uint32_t matrix_size;
    uint32_t thread_count;
    uint32_t data_length;
};

int sendAll(SOCKET sock, const char* data, int length) {
    int total = 0;
    while (total < length) {
        int sent = send(sock, data + total, length - total, 0);
        if (sent <= 0) return -1;
        total += sent;
    }
    return total;
}

void clientThread() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr));

    send(sock, "HELLO", 5, 0);
    char buffer[1024];
    recv(sock, buffer, 1024, 0);
    cout << "[SERVER] "  << buffer << endl;

    int matrixSize = 20000;
    vector matrix(matrixSize, vector(matrixSize, rand() % 1001));
    vector threadConfig = { 1, 4, 8, 16, 32, 64, 128 };

    send(sock, "SEND_DATA", 9, 0);

    DataHeader header;
    header.matrix_size = htonl(matrixSize);
    header.thread_count = htonl(threadConfig.size());
    header.data_length = htonl(matrixSize * matrixSize * sizeof(int));

    if (sendAll(sock, reinterpret_cast<char*>(&header), sizeof(header)) != sizeof(header)) {
        cerr << "Failed to send header" << endl;
        return;
    }

    vector<int> configNetwork(threadConfig.size());
    for (size_t i = 0; i < threadConfig.size(); ++i) {
        configNetwork[i] = htonl(threadConfig[i]);
    }
    if (sendAll(sock, reinterpret_cast<char*>(configNetwork.data()),
              configNetwork.size() * sizeof(int)) != configNetwork.size() * sizeof(int)) {
        cerr << "Failed to send thread config" << endl;
        return;
    }

    vector<int> flatMatrix;
    for (const auto& row : matrix) {
        flatMatrix.insert(flatMatrix.end(), row.begin(), row.end());
    }
    vector<int> flatNetwork(flatMatrix.size());
    for (size_t i = 0; i < flatMatrix.size(); ++i) {
        flatNetwork[i] = htonl(flatMatrix[i]);
    }
    if (sendAll(sock, reinterpret_cast<char*>(flatNetwork.data()),
              flatNetwork.size() * sizeof(int)) != flatNetwork.size() * sizeof(int)) {
        cerr << "Failed to send matrix" << endl;
        return;
    }

    recv(sock, buffer, 1024, 0);
    cout << "[SERVER] "  << buffer << endl;

    send(sock, "START_COMPUTATION", 17, 0);
    atomic<bool> isComputationComplete(false);

    thread notificationThread([&sock, &isComputationComplete]() {
        char buffer[1024];
        while (!isComputationComplete) {
            int bytes = recv(sock, buffer, 1024, 0);
            if (bytes <= 0) break;
            string response(buffer, bytes);
            cout << "[SERVER] " << response << endl;
            if (response.find("COMPUTATION_COMPLETE") != string::npos) {
                isComputationComplete = true;
            }
        }
    });

    cout << "Press Enter to get status..." << endl;
    while (!isComputationComplete) {
        cin.ignore();
        send(sock, "GET_STATUS", 10, 0);
        int bytes = recv(sock, buffer, 1024, 0);
        if (bytes > 0) {
            cout << "[SERVER] " << string(buffer, bytes) << endl;
        }
    }
    closesocket(sock);
    WSACleanup();
}

int main() {
    thread(clientThread).join();
    return 0;
}