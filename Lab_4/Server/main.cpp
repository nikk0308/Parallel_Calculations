#include <iostream>
#include <vector>
#include <thread>
#include <winsock2.h>
#include <chrono>
#include <map>

using namespace std;
using namespace chrono;

struct DataHeader {
    uint32_t matrix_size;
    uint32_t thread_count;
    uint32_t data_length;
};

struct ClientData {
    vector<vector<int>> matrix;
    vector<int> thread_config;
    vector<double> results;
    int current_thread = 0;
    bool is_processing = false;
};

map<SOCKET, ClientData> clients;

int recvAll(SOCKET sock, char* buffer, int length) {
    int total = 0;
    while (total < length) {
        int received = recv(sock, buffer + total, length - total, 0);
        if (received <= 0) return -1;
        total += received;
    }
    return total;
}

void processMatrixSection(int startRow, int endRow, const vector<vector<int>>& primaryMatrix, vector<vector<int>>& matrix) {
    for (int i = startRow; i < endRow; ++i) {
        int rowSum = 0;
        for (int j = 0; j < matrix.size(); ++j) {
            rowSum += primaryMatrix[i][j];
        }
        matrix[i][i] = rowSum;
    }
}

void handleClient(SOCKET clientSocket) {
    ClientData& data = clients[clientSocket];
    char buffer[8192];

    try {
        while (true) {
            int bytesReceived = recv(clientSocket, buffer, 8192, 0);
            if (bytesReceived <= 0) break;

            string command(buffer, bytesReceived);
            cout << "[CLIENT #" << clientSocket << "] " << command << endl;

            if (command == "HELLO") {
                send(clientSocket, "CONNECTED", 10, 0);
            }
            else if (command == "SEND_DATA") {
                DataHeader header;
                if (recvAll(clientSocket, reinterpret_cast<char*>(&header), sizeof(header)) != sizeof(header)) {
                    throw runtime_error("Invalid header");
                }
                header.matrix_size = ntohl(header.matrix_size);
                header.thread_count = ntohl(header.thread_count);
                header.data_length = ntohl(header.data_length);

                if (header.data_length != header.matrix_size * header.matrix_size * sizeof(int)) {
                    throw runtime_error("Data length mismatch");
                }

                vector<int> threadConfig(header.thread_count);
                if (recvAll(clientSocket, reinterpret_cast<char*>(threadConfig.data()),
                          header.thread_count * sizeof(int)) != header.thread_count * sizeof(int)) {
                    throw runtime_error("Failed to receive thread config");
                }
                for (int& t : threadConfig) {
                    t = ntohl(t);
                }

                vector<int> flatMatrix(header.matrix_size * header.matrix_size);
                if (recvAll(clientSocket, reinterpret_cast<char*>(flatMatrix.data()),
                          header.data_length) != header.data_length) {
                    throw runtime_error("Failed to receive matrix data");
                }

                data.matrix.resize(header.matrix_size, vector<int>(header.matrix_size));
                for (int i = 0; i < header.matrix_size; ++i) {
                    for (int j = 0; j < header.matrix_size; ++j) {
                        data.matrix[i][j] = ntohl(flatMatrix[i * header.matrix_size + j]);
                    }
                }

                data.thread_config = threadConfig;
                send(clientSocket, "DATA_RECEIVED", 14, 0);
            }
            else if (command == "START_COMPUTATION") {
                if (data.matrix.empty() || data.thread_config.empty()) {
                    throw runtime_error("Data not initialized");
                }

                data.is_processing = true;
                thread([&data, clientSocket]() {
                    for (size_t i = 0; i < data.thread_config.size(); ++i) {
                        data.current_thread = i;
                        int threadsCount = data.thread_config[i];
                        auto start = high_resolution_clock::now();

                        vector<vector<int>> copiedMatrix = data.matrix;
                        vector<thread> threads;
                        int rowsPerThread = copiedMatrix.size() / threadsCount;
                        int extraRows = copiedMatrix.size() % threadsCount;

                        for (int t = 0; t < threadsCount; ++t) {
                            int startRow = t * rowsPerThread + min(t, extraRows);
                            int endRow = startRow + rowsPerThread + (t < extraRows ? 1 : 0);
                            threads.emplace_back(processMatrixSection, startRow, endRow, cref(data.matrix), ref(copiedMatrix));
                        }
                        for (auto& th : threads) th.join();

                        auto end = high_resolution_clock::now();
                        double elapsed = duration_cast<nanoseconds>(end - start).count() * 1e-9;
                        data.results.push_back(elapsed);

                        string progress = "PROGRESS: " + to_string(threadsCount) + " threads, time: " + to_string(elapsed);
                        send(clientSocket, progress.c_str(), progress.size(), 0);
                    }
                    data.is_processing = false;
                    send(clientSocket, "COMPUTATION_COMPLETE", 20, 0);
                    cout << "[CLIENT #" << clientSocket << "] " << "COMPUTATION_COMPLETE" << endl;
                }).detach();

                send(clientSocket, "COMPUTATION_STARTED", 19, 0);

            }
            else if (command == "GET_STATUS") {
                if (!data.is_processing) {
                    send(clientSocket, "STATUS: COMPLETED", 17, 0);
                }
                else {
                    string status = "STATUS: " + to_string(data.current_thread + 1) + "/" +
                                   to_string(data.thread_config.size());
                    send(clientSocket, status.c_str(), status.size(), 0);
                }

            } else if (command == "GET_RESULT") {
                string result = "RESULT: \n";
                result += "Matrix size: " + to_string(data.matrix.size()) + "x" + to_string(data.matrix.size());
                for (size_t i = 0; i < data.thread_config.size(); ++i) {
                    result += "\n" + to_string(data.thread_config[i]) + "\t threads: " + to_string(data.results[i]) + " seconds";
                }
                send(clientSocket, result.c_str(), result.size() + 1, 0);
            }
        }
    } catch (const exception& e) {
        cerr << "[ERROR] Client " << clientSocket << ": " << e.what() << endl;
        send(clientSocket, "ERROR", 5, 0);
    }

    closesocket(clientSocket);
    clients.erase(clientSocket);
}

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, 5);

    cout << "Server started on port 8080" << endl;

    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        cout << "[SERVER] New client connected. Socket: " << clientSocket << endl;
        thread(handleClient, clientSocket).detach();
    }
}