#pragma once

#include <chrono>
#include <string>

// 业务会话数据，框架不感知，通过 Tcpconnection::setContext 挂在连接上
struct ClientSession {
    std::string username;
    std::chrono::steady_clock::time_point lastActive; // 最后活跃时间
    bool loggedIn = false;
};
