#pragma once

#include <string>

// 业务会话数据，框架不感知，通过 Tcpconnection::setContext 挂在连接上
struct ClientSession {
    std::string username;
    bool loggedIn = false;
};
