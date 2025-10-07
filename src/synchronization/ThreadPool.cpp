#include "ThreadPool.h"
#include "Logger.h"

ThreadPool::ThreadPool(size_t threads) : stop_flag(false), max_threads(threads) {
    scaleThreads(threads);
    Logger::log("ThreadPool initialized with " + std::to_string(threads) + " threads");
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queue_mtx);
        tasks.push(task);
    }
    cv.notify_one();
}

void ThreadPool::stop() {
    {
        std::unique_lock<std::mutex> lock(queue_mtx);
        stop_flag = true;
    }
    cv.notify_all();
    for (auto& worker : workers) {
        if (worker.joinable()) worker.join();
    }
}

void ThreadPool::scaleThreads(size_t new_size) {
    {
        std::unique_lock<std::mutex> lock(queue_mtx);
        max_threads = new_size;
        while (workers.size() < max_threads) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mtx);
                        cv.wait(lock, [this] { return stop_flag || !tasks.empty(); });
                        if (stop_flag && tasks.empty()) return;
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
            });
        }
    }
    Logger::log("Scaled ThreadPool to " + std::to_string(max_threads) + " threads");
}