#include <iostream>
#include <queue>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <chrono>
#include <shared_mutex>
#include <atomic>

using namespace std;

using read_write_lock = shared_mutex;
using read_lock = shared_lock<read_write_lock>;
using write_lock = unique_lock<read_write_lock>;

atomic<bool> stop_program(false);
atomic<int> global_task_id(0);
mutex cout_mutex;

struct PrioritizedTask {
    int priority;
    function<void()> task;

    bool operator<(const PrioritizedTask& other) const {
        return priority > other.priority;
    }
};

template <typename task_type_t>
class task_queue {
    using task_queue_implementation = priority_queue<task_type_t>;

public:
    task_queue() = default;
    ~task_queue() { clear(); }

    bool empty() const {
        read_lock lock(m_rw_lock);
        return m_tasks.empty();
    }

    task_type_t top() const {
        read_lock lock(m_rw_lock);
        return m_tasks.top();
    }

    size_t size() const {
        read_lock lock(m_rw_lock);
        return m_tasks.size();
    }

    void clear() {
        write_lock lock(m_rw_lock);
        while (!m_tasks.empty()) {
            m_tasks.pop();
        }
    }

    bool pop(task_type_t& task) {
        write_lock lock(m_rw_lock);
        if (m_tasks.empty()) {
            return false;
        } else {
            task = m_tasks.top();
            m_tasks.pop();
            return true;
        }
    }

    bool pop() {
        write_lock lock(m_rw_lock);
        if (m_tasks.empty()) {
            return false;
        } else {
            m_tasks.pop();
            return true;
        }
    }

    template <typename... arguments>
    void emplace(arguments&&... parameters) {
        write_lock lock(m_rw_lock);
        m_tasks.emplace(forward<arguments>(parameters)...);
    }

private:
    mutable read_write_lock m_rw_lock;
    task_queue_implementation m_tasks;
};

class thread_pool {
public:
    thread_pool() = default;
    ~thread_pool() { terminate(); }

    void initialize(const size_t worker_count) {
        write_lock lock(m_rw_lock);
        if (m_initialized || m_terminated) {
            return;
        }
        m_workers.reserve(worker_count);
        for (size_t id = 0; id < worker_count; ++id) {
            m_workers.emplace_back(&thread_pool::routine, this);
        }
        m_initialized = !m_workers.empty();
    }

    void terminate() {
        {
            write_lock lock(m_rw_lock);
            if (working_unsafe()) {
                m_terminated = true;
                m_force_terminate = true;
            } else {
                m_workers.clear();
                m_terminated = false;
                m_initialized = false;
                return;
            }
        }
        m_task_waiter.notify_all();
        for (thread& worker : m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        m_workers.clear();
        m_terminated = false;
        m_initialized = false;
    }

    template <typename task_t, typename... arguments>
    void add_task(int priority, task_t&& task, arguments&&... parameters) {
        {
            read_lock lock(m_rw_lock);
            if (!working_unsafe()) {
                return;
            }
        }
        auto bind = std::bind(forward<task_t>(task), forward<arguments>(parameters)...);
        m_tasks.emplace(PrioritizedTask{priority, bind});
        m_task_waiter.notify_one();
        ++total_tasks_created;
    }

    void pause() {
        write_lock lock(m_rw_lock);
        m_paused = true;
    }

    void resume() {
        {
            write_lock lock(m_rw_lock);
            m_paused = false;
        }
        m_task_waiter.notify_all();
    }

    size_t get_total_tasks_created() const { return total_tasks_created.load(); }
    size_t get_total_tasks_completed() const { return total_tasks_completed.load(); }
    double get_average_queue_length() const {
        return static_cast<double>(total_queue_length.load()) / total_tasks_created.load();
    }
    double get_average_wait_time() const {
        return static_cast<double>(total_wait_time.load()) / (total_tasks_completed.load() * 1000);
    }

private:
    mutable read_write_lock m_rw_lock;
    mutable condition_variable_any m_task_waiter;
    vector<thread> m_workers;
    task_queue<PrioritizedTask> m_tasks;
    bool m_initialized = false;
    bool m_terminated = false;
    bool m_paused = false;
    bool m_force_terminate = false;

    atomic<size_t> total_tasks_created{0};
    atomic<size_t> total_tasks_completed{0};
    atomic<size_t> total_queue_length{0};
    atomic<size_t> total_wait_time{0};

    void routine() {
        while (true) {
            PrioritizedTask task;
            {
                write_lock lock(m_rw_lock);
                auto wait_condition = [this] {
                    return m_terminated || (!m_tasks.empty() && !m_paused);
                };
                m_task_waiter.wait(lock, wait_condition);

                if (m_force_terminate) {
                    return;
                }

                if (!m_tasks.empty()) {
                    task = m_tasks.top();
                    m_tasks.pop();
                    total_queue_length += m_tasks.size();
                }
            }

            if (task.task) {
                auto start_time = chrono::steady_clock::now();
                task.task();
                auto end_time = chrono::steady_clock::now();
                total_wait_time += chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
                ++total_tasks_completed;
            }

            if (m_force_terminate) {
                return;
            }
        }
    }

    bool working() const {
        read_lock lock(m_rw_lock);
        return working_unsafe();
    }

    bool working_unsafe() const {
        return m_initialized && !m_terminated;
    }
};

void executeTask(int taskId, int duration) {
    {
        lock_guard lock(cout_mutex);
        cout << "Task " << taskId << " started, duration: " << duration << " seconds.\n";
    }
    this_thread::sleep_for(chrono::seconds(duration));
    {
        lock_guard lock(cout_mutex);
        cout << "Task " << taskId << " completed.\n";
    }
}

int getRandomDuration(int min, int max) {
    return min + rand() % (max - min + 1);
}

int getRandomInterval(int min, int max) {
    return min + rand() % (max - min + 1);
}

void autoTerminateAfterTime(int seconds) {
    this_thread::sleep_for(chrono::seconds(seconds));
    stop_program = true;
}

int main() {
    const int workers_amount = 4;
    const int min_task_time = 5;
    const int max_task_time = 10;

    const int generation_rate_freq = 2;
    const int num_generators = 4;
    const int simulation_duration = 20;

    srand(static_cast<unsigned>(time(nullptr)));

    thread_pool pool;
    pool.initialize(workers_amount);

    atomic<bool> stop_generation(false);

    auto generate_tasks = [&pool, &stop_generation, generation_rate_freq]() {
        while (!stop_generation) {
            int duration = getRandomDuration(min_task_time, max_task_time);
            int task_id = global_task_id.fetch_add(1);
            pool.add_task(duration, executeTask, task_id, duration);

            int interval = getRandomInterval(1, 2) * generation_rate_freq;
            this_thread::sleep_for(chrono::seconds(interval));
        }
    };

    vector<thread> generators;
    for (size_t i = 0; i < num_generators; ++i) {
        generators.emplace_back(generate_tasks);
    }

    thread timer_thread(autoTerminateAfterTime, simulation_duration);

    while (!stop_program) {
        this_thread::sleep_for(chrono::milliseconds(100));
    }

    stop_generation = true;
    pool.terminate();

    for (auto& generator : generators) {
        if (generator.joinable()) {
            generator.join();
        }
    }

    if (timer_thread.joinable()) {
        timer_thread.join();
    }

    cout << endl;
    cout << "Total tasks created: " << pool.get_total_tasks_created() << endl;
    cout << "Total tasks completed: " << pool.get_total_tasks_completed() << endl;
    cout << "Average queue length: " << pool.get_average_queue_length() << endl;
    cout << "Average task execution time: " << pool.get_average_wait_time() << " s" <<endl;

    return 0;
}