#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <queue>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>

class ThreadPool {
public:
    ThreadPool(size_t threads);
    ~ThreadPool();
    void enqueue(std::function<void()> task);
    void stop();
    void scaleThreads(size_t new_size);

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mtx;
    std::condition_variable cv;
    bool stop_flag;
    size_t max_threads;
};

#endif