#ifndef IPC_MANAGER_H
#define IPC_MANAGER_H

#include <string>
#include <mqueue.h>

class IPCManager {
public:
    IPCManager();
    ~IPCManager();
    void sendMessage(const std::string& message);
    std::string receiveMessage();

private:
    mqd_t mq;
};

#endif