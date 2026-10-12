#include "TcpServer.hpp"

#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

#include "Acceptor.hpp"
#include "Commands.hpp"
#include "EventLoop.hpp"
#include "EventLoopThreadPool.hpp"
#include "EventLoopThread.hpp"
#include "Tcpconnection.hpp"
#include "Timer.hpp"
#include "UserStruct.hpp"

//每个loop各自的上下文空间。
//对象由 TcpServer 的 contexts_/ctxByLoop_ 持有引用计数，
//但必须在归属 IO 线程构造和析构：TimerQueue 内部 timerfd 要操作该线程的 epoll。
struct TcpServer::LoopContext {
    explicit LoopContext(EventLoop* owner, TcpServer* server)
        : owner_(owner), tq_(owner) {
        //心跳60s超时：每秒检查一次本线程连接表
        tq_.addTimer([this, server] {
            //先收集再关闭，避免遍历 map 时直接 erase 导致迭代器失效
            std::vector<Tcpconnection::Ptr> dead;
            auto now = Clock::now();

            for (auto& [fd, conn] : conns_) {
                auto session = conn->getContext<ClientSession>();

                if (session &&
                    session->loggedIn &&
                    now - session->lastActive > std::chrono::seconds(60)) {
                    dead.push_back(conn);
                }
            }

            for (auto& conn : dead) {
                std::cout << "[Heartbeat] 心跳超时，fd="
                          << conn->getFd() << '\n';
                conn->forceClose();
            }
        }, std::chrono::seconds(1), std::chrono::seconds(1));
    }

    //属于哪个eventloop
    EventLoop* owner_;
    //对应的定时器
    TimerQueue tq_;
    //这个epoll实例对应的线程管理的连接
    std::unordered_map<int, Tcpconnection::Ptr> conns_;
};

TcpServer::TcpServer(EventLoop* baseLoop, uint16_t port, int numThreads)
    : baseLoop_(baseLoop),
      port_(port),
      //创建一个线程池对象，用unique ptr
      pool_(std::make_unique<EventLoopThreadPool>(baseLoop_, "ioPool")) {
    pool_->setThreadNum(numThreads);
}

TcpServer::~TcpServer() {
    stop();
}

//注入业务消息回调
void TcpServer::setMessageCallback(MessageCallback cb) {
    messageCb_ = std::move(cb);
}

//注入连接建立回调
void TcpServer::setConnectionCallback(ConnectionCallback cb) {
    connCb_ = std::move(cb);
}

//在 loop 自己的线程构造工作间；主线程通过 future 等构造完成并拿回指针
std::shared_ptr<TcpServer::LoopContext>
TcpServer::createContextInLoop(EventLoop* loop) {
    //0 个 IO 线程时，base loop 自己干活，直接在当前线程构造
    if (loop == baseLoop_ && pool_->getLoops().empty()) {
        return std::make_shared<LoopContext>(loop, this);
    }

    //promise 是跨线程传递一次性结果的"写入端"：
    //一个线程往里面放结果，另一个线程通过配对的 future 等结果，
    //自带"放了就通知、没放就阻塞"，不用自己写 mutex + condition_variable。
    std::promise<std::shared_ptr<LoopContext>> promise;
    auto future = promise.get_future();

    //queueInLoop只负责让任务进入对应loop的任务队列，此时任务可能尚未执行，需要主线程等待任务执行
    loop->queueInLoop([loop, this, &promise] {
        //这一步相当于创建了一个LoopContext实例，但是暂时存储在promise里面
        promise.set_value(std::make_shared<LoopContext>(loop, this));
    });

    //拿到结果
    return future.get();
}

//在 loop 自己的线程销毁工作间，保证 TimerQueue 析构也发生在 owner 线程
void TcpServer::destroyContext(EventLoop* loop, std::shared_ptr<LoopContext> ctx) {
    //0 个 IO 线程时直接在当前线程释放
    if (loop == baseLoop_ && pool_->getLoops().empty()) {
        auto it = ctxByLoop_.find(baseLoop_);
        if (it != ctxByLoop_.end()) {
            ctxByLoop_.erase(it);
        }
        contexts_.erase(std::find(contexts_.begin(), contexts_.end(), ctx));
        ctx.reset();
        return;
    }

    auto it = ctxByLoop_.find(loop);
    if (it == ctxByLoop_.end()) {
        return;
    }

    ctxByLoop_.erase(it);
    contexts_.erase(std::find(contexts_.begin(), contexts_.end(), ctx));

    std::promise<void> done;
    auto future = done.get_future();

    //把最后一份 ctx 移动进任务，在 owner 线程 reset 释放
    loop->queueInLoop([ctx = std::move(ctx), &done]() mutable {
        ctx.reset();
        done.set_value();
    });

    future.get();
}

void TcpServer::start() {
    //原子地把 started_ 设为 true，并返回设置前的值
    if (started_.exchange(true)) {
        return;
    }

    //调用线程池的开始函数
    //会创建对应数量的线程
    //被创建的线程会被存储在pool自己的vector中被持有
    pool_->start();

    //如果threadpool为空，说明没有任何子线程
    if (pool_->getLoops().empty()) {
        //创建base线程自己的上下文，并且进行存储
        auto ctx = createContextInLoop(baseLoop_);
        contexts_.push_back(ctx);
        ctxByLoop_[baseLoop_] = ctx;
    } else {
        //走到这里说明pool里面存在子线程
        for (EventLoop* loop : pool_->getLoops()) {
            //拿到结果，并且进行push
            //创建每个子线程自己的上下文空间
            auto ctx = createContextInLoop(loop);
            contexts_.push_back(ctx);
            ctxByLoop_[loop] = ctx;
        }
    }
    //创建真实的listen对象
    acceptor_ = std::make_unique<Acceptor>(baseLoop_, port_);

    //设置accept的回调函数，用来处理拿到的连接
    acceptor_->setCallback([this](int fd, const std::string& ip, uint16_t port) {
        newConnection(fd, ip, port);
    });

    acceptor_->listen();
}

void TcpServer::stop() {
    if (!started_.exchange(false)) {
        return;
    }

    //先停 Acceptor，不再产生新连接
    if (acceptor_) {
        acceptor_.reset();
    }

    //再在每个 IO 线程释放自己的工作间
    if (pool_->getLoops().empty()) {
        auto it = ctxByLoop_.find(baseLoop_);
        if (it != ctxByLoop_.end()) {
            destroyContext(baseLoop_, it->second);
        }
    } else {
        for (EventLoop* loop : pool_->getLoops()) {
            auto it = ctxByLoop_.find(loop);
            if (it != ctxByLoop_.end()) {
                destroyContext(loop, it->second);
            }
        }
    }

    //最后回收线程池
    pool_.reset();
}

//accept 到新连接：base 线程选 owner loop，再把"创建+注册+入表"整体投递过去
void TcpServer::newConnection(int fd, const std::string& ip, uint16_t port) {
    //取出一个线程的eventloop
    EventLoop* ioLoop = pool_->getNextLoop();
    //在map中查找对应的pair<eventloop* ,loopcontext>
    auto it = ctxByLoop_.find(ioLoop);

    //如果没有找到
    if (it == ctxByLoop_.end()) {
        ::close(fd);
        return;
    }

    //找到了，拿到上下文空间，ctx为指针(shared_ptr)
    auto ctx = it->second;

    //在这个线程的eventloop进行任务推送，不会推送到其他线程的任务队列
    //在C++17后，执行下面这一行，在进入queueInLoop函数体之前，已经有一个临时变量作为lambda的闭包临时变量，
    //就已经让sharedptr+1了
    //以下整体在 owner IO 线程执行；局部数据全部按值捕获
    ioLoop->queueInLoop([this, ctx, ioLoop, fd, ip = ip, port] {
        //采取拷贝捕获和引用捕获
        //创建新连接的tcpconnection对象
        auto conn = std::make_shared<Tcpconnection>(ioLoop, fd, ip, port);
        //先用弱指针
        std::weak_ptr<Tcpconnection> weak = conn;
        ctx->tq_.addTimer([weak] {
            //必要的时候升级，避免长时间持有对象
            if (auto c = weak.lock()) {
                c->sendPacket(kCmdNotice, "connected 8s, 这是定时公告");
            }
        }, std::chrono::seconds(8));//默认一次性发送

        //1. 先注入回调（必须在注册进 epoll 之前）
        //close 回调内部固定为连接表移除，这是服务器内务
        conn->setCloseCallback([this](const Tcpconnection::Ptr& c) {
            removeConnection(c);
        });
        //message 回调转交给业务层注入的 messageCb_
        conn->setMessageCallback([this](const Tcpconnection::Ptr& c, Buffer* buffer) {
            if (messageCb_) {
                messageCb_(c, buffer);
            }
        });

        //2. 注册进 epoll（内部完成 tie 并关注读事件）
        conn->connectionSuccess();
        //3. 入表，由本线程工作间持有
        ctx->conns_[fd] = conn;

        //连接建立通知（可选）
        if (connCb_) {
            connCb_(conn);
        }
    });
}

//连接关闭后：定位 owner 工作间，从连接表 erase
void TcpServer::removeConnection(const std::shared_ptr<Tcpconnection>& conn) {
    auto it = ctxByLoop_.find(conn->getLoop());

    if (it != ctxByLoop_.end()) {
        it->second->conns_.erase(conn->getFd());
    }
}
