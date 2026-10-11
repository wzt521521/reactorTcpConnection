#include <cstdint>      // uint16_t
#include <memory>       // std::make_shared / std::shared_ptr
#include <string>
#include <unordered_map>
#include "Acceptor.hpp"
#include "Commands.hpp"
#include "Dispatcher.hpp"
#include "EventLoop.hpp"
#include "Packet.hpp"
#include "Tcpconnection.hpp"
#include "UserStruct.hpp"

int main() {
    EventLoop loop;
    Acceptor accept(&loop, 8080);
    MessageDispatcher dispatcher;
    Packet codec;
    std::unordered_map<int, Tcpconnection::Ptr> connections;

    dispatcher.registerHandler(kCmdEcho, [&](const Tcpconnection::Ptr& conn, uint16_t cmd, const std::string& body) {
        conn->sendPacket(cmd, body);
    });

    dispatcher.registerHandler(kCmdLogin, [&](const Tcpconnection::Ptr& conn, uint16_t /*cmd*/, const std::string& body) {
        auto it = std::make_shared<ClientSession>();
        it->username = body;
        it->loggedIn = true;
        conn->setContext(it);
        conn->sendPacket(kCmdLogin, "welcome " + body);
    });

    dispatcher.registerHandler(kCmdChat,
        [&](const Tcpconnection::Ptr& conn, uint16_t /*cmd*/, const std::string& body) {
            auto s = conn->getContext<ClientSession>();
            if (!s || !s->loggedIn) {
                conn->sendPacket(kCmdError, "not logged in");
                return;
            }
            conn->sendPacket(kCmdChat, s->username + " 说: " + body);
        });

    // decode 切出完整帧后的回调：此时帧已从 buffer 解析好，交给分发器按 cmd 路由
    codec.setFrameCallback([&](const Tcpconnection::Ptr& conn, uint16_t cmd, const std::string& body) {
        dispatcher.dispatch(conn, cmd, body);
    });

    accept.setCallback([&](int fd, const std::string& ip, uint16_t port) {
        // 拿到新连接的 ip 和 port，创建 Tcpconnection
        auto conn = std::make_shared<Tcpconnection>(&loop, fd, ip, port);
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
