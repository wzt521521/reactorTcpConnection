#include <chrono>        // std::chrono
#include <cstdint>      // uint16_t
#include <iostream>      // std::cout / std::endl
#include <memory>       // std::make_shared / std::shared_ptr
#include <string>
#include <unordered_map>
#include "Acceptor.hpp"
#include "Commands.hpp"
#include "Dispatcher.hpp"
#include "EventLoop.hpp"
#include "Packet.hpp"
#include "Tcpconnection.hpp"
#include "Timer.hpp"
#include "UserStruct.hpp"

int main() {
    EventLoop loop;
    Acceptor accept(&loop, 8080);
    TimerQueue timerQueue(&loop);
    MessageDispatcher dispatcher;
    Packet codec;
    std::unordered_map<int, Tcpconnection::Ptr> connections;
    //刷新最后的活跃时间
    auto touch = [](const Tcpconnection::Ptr& c) {
        if (auto s = c->getContext<ClientSession>())
            s->lastActive = std::chrono::steady_clock::now();
        };

    //注册3种业务行为
    dispatcher.registerHandler(kCmdEcho, [&](const Tcpconnection::Ptr& conn, uint16_t cmd, const std::string& body) {
        conn->sendPacket(cmd, body);
        touch(conn);
    });

    dispatcher.registerHandler(kCmdLogin, [&](const Tcpconnection::Ptr& conn, uint16_t /*cmd*/, const std::string& body) {
        auto it = std::make_shared<ClientSession>();
        it->username = body;
        it->loggedIn = true;
        conn->setContext(it);
        conn->sendPacket(kCmdLogin, "welcome " + body);
        touch(conn);
    });

    dispatcher.registerHandler(kCmdChat,
        [&](const Tcpconnection::Ptr& conn, uint16_t /*cmd*/, const std::string& body) {
            auto s = conn->getContext<ClientSession>();
            if (!s || !s->loggedIn) {
                conn->sendPacket(kCmdError, "not logged in");
                return;
            }
            conn->sendPacket(kCmdChat, s->username + " 说: " + body);
            touch(conn);
        });

    //心跳60s超时
    timerQueue.addTimer([&] {
        std::vector<Tcpconnection::Ptr> dead;
        auto now = std::chrono::steady_clock::now();
        for (auto& [fd, conn] : connections) {
            auto s = conn->getContext<ClientSession>();
            if (s && s->loggedIn && now - s->lastActive > std::chrono::seconds(60))
                dead.push_back(conn);
        }
        for (auto& c : dead) {
            std::cout << "[Timer] 心跳超时，踢掉 fd=" << c->getFd() << std::endl;
            c->forceClose();
        }
        }, std::chrono::seconds(1), std::chrono::seconds(1));  // 首次1秒后，之后每1秒

    // decode 切出完整帧后的回调：此时帧已从 buffer 解析好，交给分发器按 cmd 路由
    codec.setFrameCallback([&](const Tcpconnection::Ptr& conn, uint16_t cmd, const std::string& body) {
        dispatcher.dispatch(conn, cmd, body);
    });

    accept.setCallback([&](int fd, const std::string& ip, uint16_t port) {
        // 拿到新连接的 ip 和 port，创建 Tcpconnection
        auto conn = std::make_shared<Tcpconnection>(&loop, fd, ip, port);
        std::weak_ptr<Tcpconnection> weak = conn;
        timerQueue.addTimer([weak] {
            if (auto c = weak.lock())
                c->sendPacket(kCmdNotice, "connected 8s, 这是定时公告");
            }, std::chrono::seconds(8));
        // 1. 先注入两个回调（必须在注册进 epoll 之前）
        conn->setCloseCallback([&](const Tcpconnection::Ptr& c) {
            connections.erase(c->getFd()); // 从全局引用表删除
        });
        conn->setMessageCallback([&codec](const Tcpconnection::Ptr& c, Buffer* b) {
            // 数据已读到 buffer，未做任何业务处理，持续交给 decode 切包
            // decode 返回 false 表示遇到非法帧，直接断开连接
            if (!codec.decode(c, b))
                c->forceClose();
        });
        // 2. 注册进 epoll（内部完成 tie 并关注读事件）
        conn->connectionSuccess();
        // 3. 入表，由全局表持有
        connections[fd] = conn;
    });

    accept.listen();
    loop.Loops();
    return 0;
}
