#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "Commands.hpp"
#include "Dispatcher.hpp"
#include "EventLoop.hpp"
#include "Packet.hpp"
#include "TcpServer.hpp"
#include "Tcpconnection.hpp"
#include "Timer.hpp"
#include "UserStruct.hpp"

int main() {
    EventLoop baseLoop;//创建用于主线程监听的epoll，用来管理新连接
    TcpServer server(&baseLoop, 8080, 4);

    MessageDispatcher dispatcher;
    Packet codec;

    //工具函数，用于更新user的最后活跃时间
    auto touch = [](const Tcpconnection::Ptr& conn) {
        if (auto session = conn->getContext<ClientSession>()) {
            session->lastActive = Clock::now();
        }
        };

    //echo函数，简单echo
    dispatcher.registerHandler(kCmdEcho,
        [&](const Tcpconnection::Ptr& conn, uint16_t cmd, const std::string& body) {
            conn->sendPacket(cmd, body);
            touch(conn);
        });

    //登录函数
    dispatcher.registerHandler(kCmdLogin,
        [&](const Tcpconnection::Ptr& conn, uint16_t /*cmd*/, const std::string& body) {
            auto session = std::make_shared<ClientSession>();
            session->username = body;
            session->loggedIn = true;
            //用来让tcpconnection获取到对应的业务对象
            conn->setContext(session);
            conn->sendPacket(kCmdLogin, "welcome " + body);
            touch(conn);
        });

    //char函数
    dispatcher.registerHandler(kCmdChat,
        [&](const Tcpconnection::Ptr& conn, uint16_t /*cmd*/, const std::string& body) {
            auto session = conn->getContext<ClientSession>();

            if (!session || !session->loggedIn) {
                conn->sendPacket(kCmdError, "not logged in");
                return;
            }

            conn->sendPacket(kCmdChat, session->username + " 说: " + body);
            touch(conn);
        });

    //设置编码完成后的业务回调，执行此函数，说明decode成功了，拿到了一个完整的报文
    codec.setFrameCallback([&](const Tcpconnection::Ptr& conn,
        uint16_t cmd,
        const std::string& body) {
            dispatcher.dispatch(conn, cmd, body);
        });

    //消息入口：数据读到 buffer 后持续交给 decode 切包；非法帧直接断开
    server.setMessageCallback([&](const Tcpconnection::Ptr& conn, Buffer* buffer) {
        if (!codec.decode(conn, buffer)) {
            conn->forceClose();
        }
        });

    server.start();
    baseLoop.Loops();

    return 0;
}
