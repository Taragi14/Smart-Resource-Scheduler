#include "IPCManager.h"
#include "Logger.h"
#include <fcntl.h>
#include <sys/stat.h>

IPCManager::IPCManager() {
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = 256;
    attr.mq_curmsgs = 0;
    mq = mq_open("/smart_scheduler_mq", O_CREAT | O_RDWR, 0644, &attr);
    if (mq == -1) {
        Logger::log("Failed to open message queue");
    }
}

IPCManager::~IPCManager() {
    mq_close(mq);
    mq_unlink("/smart_scheduler_mq");
}

void IPCManager::sendMessage(const std::string& message) {
    if (mq_send(mq, message.c_str(), message.size(), 0) == -1) {
        Logger::log("Failed to send message: " + message);
    } else {
        Logger::log("Sent message: " + message);
    }
}

std::string IPCManager::receiveMessage() {
    char buffer[256];
    ssize_t bytes = mq_receive(mq, buffer, 256, nullptr);
    if (bytes == -1) {
        Logger::log("Failed to receive message");
        return "";
    }
    return std::string(buffer, bytes);
}