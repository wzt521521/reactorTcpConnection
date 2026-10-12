#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

class EventLoop;
class Tcpconnection;
class Buffer;

// TCP 服务器：统一持有 Acceptor、IO 线程池和每个 loop 私有的 LoopContext，
// 负责新连接的创建分发、连接移除，以及关闭时的线程回收和上下文释放。
// main 只需要注入业务回调并 start。
class TcpServer {
public:
    using MessageCallback =
        std::function<void(const std::shared_ptr<Tcpconnection>&, Buffer*)>;
    using ConnectionCallback =
        std::function<void(const std::shared_ptr<Tcpconnection>&)>;

    TcpServer(EventLoop* baseLoop, uint16_t port, int numThreads);
    ~TcpServer();

    //注入业务消息回调（decode 切包后进入业务）
    void setMessageCallback(MessageCallback cb);
    //注入连接建立回调（可选）
    void setConnectionCallback(ConnectionCallback cb);

    void start();
    void stop();

private:
    //每个loop各自的上下文空间：定时器 + 该 epoll 实例对应线程管理的连接表
    struct LoopContext;

    //在指定 loop 自己的线程构造 LoopContext
    std::shared_ptr<LoopContext> createContextInLoop(EventLoop* loop);
    //在指定 loop 自己的线程销毁 LoopContext
    void destroyContext(EventLoop* loop, std::shared_ptr<LoopContext> ctx);

    //设置accept的回调函数，用来处理拿到的连接
    void newConnection(int fd, const std::string& ip, uint16_t port);
    //连接关闭后从对应连接表移除
    void removeConnection(const std::shared_ptr<Tcpconnection>& conn);

    //创建用于主线程监听的 epoll，用来管理新连接（base loop，借用指针）
    EventLoop* baseLoop_;
    uint16_t port_;

    std::unique_ptr<class Acceptor> acceptor_;
    //线程池对象，用 unique ptr
    std::unique_ptr<class EventLoopThreadPool> pool_;

    //每个连接的上下文，用来保证存活
    std::vector<std::shared_ptr<LoopContext>> contexts_;
    //可以通过 eventloop 快速查询对应的 loopcontext，accept 前填完，之后只读
    std::unordered_map<EventLoop*, std::shared_ptr<LoopContext>> ctxByLoop_;

    MessageCallback messageCb_;
    ConnectionCallback connCb_;

    std::atomic<bool> started_{false};
};
