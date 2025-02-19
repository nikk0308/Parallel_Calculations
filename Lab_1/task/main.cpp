#include <iostream>

#include <chrono>
#include <thread>
#include <vector>

#include <windows.h>
#include <intrin.h>
#include <iomanip>

#define SeedNum 7
#define ShouldCheckCorrectness 1

using namespace std;
using chrono::nanoseconds;
using chrono::duration_cast;
using chrono::high_resolution_clock;

void printCacheInfo() {
    int CPUInfo[4];
    __cpuid(CPUInfo, 0);
    int nIds = CPUInfo[0];

    for (int i = 0; i <= nIds; ++i) {
        __cpuidex(CPUInfo, i, 0);
        if (CPUInfo[0] == 4) {
            int cacheLevel = (CPUInfo[0] >> 5) & 0x7;
            int cacheType = CPUInfo[0] & 0xFF;
            if (cacheType == 1 || cacheType == 2 || cacheType == 3) {
                cout << "Cache Level: " << cacheLevel << ", Type: "
                     << (cacheType == 1 ? "Data" : (cacheType == 2 ? "Instruction" : "Unified"))
                     << ", Size: " << ((CPUInfo[1] + 1) * 8) << " KB" << endl;
            }
        }
    }
}

void printSystemInfo(int &cpuNum) {
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    cpuNum = sysinfo.dwNumberOfProcessors;

    cout << "System Information:" << endl;
    cout << "Number of logical processors: " << sysinfo.dwNumberOfProcessors << endl;
    cout << "Processor architecture: "
         << (sysinfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL ? "x86" :
             sysinfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x64" : "Unknown")
         << endl;
    cout << "Page size: " << sysinfo.dwPageSize << " bytes" << endl;
    cout << "Minimum application address: " << sysinfo.lpMinimumApplicationAddress << endl;
    cout << "Maximum application address: " << sysinfo.lpMaximumApplicationAddress << endl;

    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);

    cout << "Total physical memory: " << memInfo.ullTotalPhys / (1024 * 1024) << " MB" << endl;
    cout << "Available physical memory: " << memInfo.ullAvailPhys / (1024 * 1024) << " MB" << endl;

    printCacheInfo();
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

bool checkMatrixCorrectness(const vector<vector<int>>& matrix, const vector<vector<int>>& primaryMatrix, int randomRowCount = 10) {
    vector<int> randomRows;
    for (int i = 0; i < randomRowCount; ++i) {
        randomRows.push_back(rand() % matrix.size());
    }
    bool isCorrect = true;
    for (auto row : randomRows) {
        if (row >= matrix.size()) continue;
        int actualSum = 0;
        for (int j = 0; j < matrix.size(); ++j) {
            actualSum += primaryMatrix[row][j];
        }
        if (matrix[row][row] != actualSum) {
            cout << "Error in row " << row << ": Expected " << actualSum << ", but got " << matrix[row][row] << endl;
            isCorrect = false;
        }
    }
    return isCorrect;
}

void linearProcessMatrix(vector<vector<int>>& matrix) {
    for (int i = 0; i < matrix.size(); ++i) {
        int rowSum = 0;
        for (int j = 0; j < matrix.size(); ++j) {
            rowSum += matrix[i][j];
        }
        matrix[i][i] = rowSum;
    }
}

int main() {
    int cpuNum;
    printSystemInfo(cpuNum);

    vector matrixSizes = {
        100,
        1000,
        5000,
        20000,
        50000,
    };

    vector numCPUArr = {
        cpuNum / 2,
        cpuNum,
        cpuNum * 2,
        cpuNum * 4,
        cpuNum * 8,
        cpuNum * 16,
    };

    cout << "\nTest Results:" << endl;
    cout << "Matrix Size\tThreads\tTime (seconds)\tCorrect?" << endl;

    for (int matrixSize : matrixSizes) {
        vector primaryMatrix(matrixSize, vector<int>(matrixSize));
        srand(SeedNum);
        for (int i = 0; i < matrixSize; ++i) {
            for (int j = 0; j < matrixSize; ++j) {
                primaryMatrix[i][j] = rand() % 10001;
            }
        }

        {
            vector<vector<int>> copiedMatrix = primaryMatrix;
            auto start = high_resolution_clock::now();
            linearProcessMatrix(copiedMatrix);
            auto end = high_resolution_clock::now();
            string correctness = ShouldCheckCorrectness ? (checkMatrixCorrectness(copiedMatrix, primaryMatrix) ? "Yes" : "No") : "Unknown";
            auto elapsed = duration_cast<nanoseconds>(end - start).count() * 1e-9;
            cout << endl << matrixSize << "\t\tLinear\t" << fixed << setprecision(6) << elapsed << "\t" << correctness << endl;
        }

        for (int i = 0; i < numCPUArr.size(); ++i) {
            vector<vector<int>> copiedMatrix = primaryMatrix;
            int threadsCount = numCPUArr[i];
            vector<thread> threads;
            auto start = high_resolution_clock::now();

            int rowsPerThread = matrixSize / threadsCount;
            int extraRows = matrixSize % threadsCount;
            for (int t = 0; t < threadsCount; ++t) {
                int startRow = t * rowsPerThread + min(t, extraRows);
                int endRow = startRow + rowsPerThread + (t < extraRows ? 1 : 0);
                threads.emplace_back(processMatrixSection, startRow, endRow, ref(primaryMatrix), ref(copiedMatrix));
            }

            for (auto &th : threads) {
                if (th.joinable()) {
                    th.join();
                }
            }

            auto end = high_resolution_clock::now();
            string correctness = ShouldCheckCorrectness ? (checkMatrixCorrectness(copiedMatrix, primaryMatrix) ? "Yes" : "No") : "Unknown";
            auto elapsed = duration_cast<nanoseconds>(end - start).count() * 1e-9;
            cout << matrixSize << "\t\t" << threadsCount << "\t" << fixed << setprecision(6) << elapsed << "\t" << correctness << endl;
        }
    }

    return 0;
}