#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <cstdlib>
#include <ctime>
#include <iomanip>

using namespace std;
using chrono::nanoseconds;
using chrono::duration_cast;
using chrono::high_resolution_clock;

void linearExecution(const vector<int> &data, long long &sum, int &minVal);
void parallelWithMutex(const vector<int> &data, long long &sum, int &minVal, int numThreads);
void parallelWithCAS(const vector<int> &data, long long &sum, int &minVal, int numThreads);

int main() {
    vector matrixSizes = {10000, 1000000, 100000000, 2000000000};
    vector threadCounts = {8, 16, 32, 64, 128, 256};

    cout << "\nTest Results:" << endl;
    cout << "Matrix Size\tThreads\tMode\tTime (seconds)\tSum\tMin Value" << endl;

    for (int matrixSize: matrixSizes) {
        vector<int> data(matrixSize);
        srand(static_cast<unsigned>(time(nullptr)));
        for (int i = 0; i < matrixSize; ++i) {
            data[i] = rand() % 1001;
        }

        long long sum = 0;
        int minVal = INT32_MAX;
        auto start = high_resolution_clock::now();
        linearExecution(data, sum, minVal);
        auto end = high_resolution_clock::now();
        double elapsed = duration_cast<nanoseconds>(end - start).count() * 1e-9;
        cout << matrixSize << "\t\t-\tLinear\t" << fixed << setprecision(6) << elapsed << "\t" << sum << "\t" << minVal << endl;

        cout << endl;

        for (int numThreads: threadCounts) {
            long long sum = 0;
            int minVal = INT32_MAX;
            auto start = high_resolution_clock::now();
            parallelWithMutex(data, sum, minVal, numThreads);
            auto end = high_resolution_clock::now();
            double elapsed = duration_cast<nanoseconds>(end - start).count() * 1e-9;
            cout << matrixSize << "\t\t" << numThreads << "\tMutex\t" << fixed << setprecision(6) << elapsed << "\t" <<
                    sum << "\t" << minVal << endl;
        }
        cout << endl;

        for (int numThreads: threadCounts) {
            long long sum = 0;
            int minVal = INT32_MAX;
            auto start = high_resolution_clock::now();
            parallelWithCAS(data, sum, minVal, numThreads);
            auto end = high_resolution_clock::now();
            double elapsed = duration_cast<nanoseconds>(end - start).count() * 1e-9;
            cout << matrixSize << "\t\t" << numThreads << "\tCAS\t" << fixed << setprecision(6) << elapsed << "\t" <<
                    sum << "\t" << minVal << endl;
        }
        cout << endl << endl;
    }

    return 0;
}

void linearExecution(const vector<int> &data, long long &sum, int &minVal) {
    sum = 0;
    minVal = INT32_MAX;
    for (int value: data) {
        if (value % 10 == 0) {
            sum += value;
            if (value < minVal) {
                minVal = value;
            }
        }
    }
}

void processSectionWithMutex(int start, int end, const vector<int> &data, long long &localSum, int &localMin, mutex &mtx) {
    long long sum = 0;
    int minVal = INT32_MAX;
    for (int i = start; i < end; ++i) {
        if (data[i] % 10 == 0) {
            sum += data[i];
            if (data[i] < minVal) {
                minVal = data[i];
            }
        }
    }
    lock_guard lock(mtx);
    localSum += sum;
    if (minVal < localMin) {
        localMin = minVal;
    }
}

void parallelWithMutex(const vector<int> &data, long long &sum, int &minVal, int numThreads) {
    sum = 0;
    minVal = INT32_MAX;
    mutex mtx;
    vector<thread> threads;

    int chunkSize = data.size() / numThreads;
    for (int t = 0; t < numThreads; ++t) {
        int start = t * chunkSize;
        int end = (t == numThreads - 1) ? data.size() : start + chunkSize;
        threads.emplace_back(processSectionWithMutex, start, end, cref(data), ref(sum), ref(minVal), ref(mtx));
    }

    for (auto &th: threads) {
        if (th.joinable()) {
            th.join();
        }
    }
}

void processSectionWithCAS(int start, int end, const vector<int> &data, atomic<long long> &atomicSum, atomic<int> &atomicMin) {
    long long localSum = 0;
    int localMin = INT32_MAX;
    for (int i = start; i < end; ++i) {
        if (data[i] % 10 == 0) {
            localSum += data[i];
            if (data[i] < localMin) {
                localMin = data[i];
            }
        }
    }

    atomicSum.fetch_add(localSum, memory_order_relaxed);

    int currentMin = atomicMin.load(memory_order_relaxed);
    while (localMin < currentMin && atomicMin.compare_exchange_weak(currentMin, localMin, memory_order_relaxed)) {
    }
}

void parallelWithCAS(const vector<int> &data, long long &sum, int &minVal, int numThreads) {
    atomic<long long> atomicSum(0);
    atomic<int> atomicMin(INT32_MAX);
    vector<thread> threads;

    int chunkSize = data.size() / numThreads;
    for (int t = 0; t < numThreads; ++t) {
        int start = t * chunkSize;
        int end = (t == numThreads - 1) ? data.size() : start + chunkSize;
        threads.emplace_back(processSectionWithCAS, start, end, cref(data), ref(atomicSum), ref(atomicMin));
    }

    for (auto &th: threads) {
        if (th.joinable()) {
            th.join();
        }
    }

    sum = atomicSum.load();
    minVal = atomicMin.load();
}