#include "Semaphore.h"
#include "Logger.h"

Semaphore::Semaphore(int count) : count(count) {
    Logger::log("Semaphore initialized with count: " + std::to_string(count));
}

void Semaphore::wait() {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [this] { return count > 0; });
    --count;
    Logger::log("Semaphore wait, count: " + std::to_string(count));
}

void Semaphore::signal() {
    std::unique_lock<std::mutex> lock(mtx);
    ++count;
    cv.notify_one();
    Logger::log("Semaphore signal, count: " + std::to_string(count));
}