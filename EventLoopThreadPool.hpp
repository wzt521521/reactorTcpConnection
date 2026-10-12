//EventLoopThreadPool
//├── EventLoopThread[0]
//│       └── std::thread[0]
//│               └── EventLoop[0]
//├── EventLoopThread[1]
//│       └── std::thread[1]
//│               └── EventLoop[1]
//└── EventLoopThread[2]
//		  └── std::thread[2]
//				  └── EventLoop[2]

//所有权关系是：
//
//```
//EventLoopThreadPool
//owns N 个 EventLoopThread
//
//EventLoopThread
//owns std::thread
//
//子线程 threadFunc 的栈
//owns EventLoop
//```
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class EventLoop;
class EventLoopThread;

class EventLoopThreadPool {
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    EventLoopThreadPool(EventLoop* baseLoop, std::string name = "");

    void setThreadNum(int numThreads);
    void start(ThreadInitCallback cb = ThreadInitCallback());

    EventLoop* getNextLoop();
    const std::vector<EventLoop*>& getLoops() const;

private:
    //线程池依附的 主事件循环指针 。它是线程池的 “宿主循环”，通常就是主线程的事件循环，
    //本身不参与线程池内的任务分发。典型场景下，端口监听、接收新连接的 Acceptor 运行在这个循环上，
    //新连接建立后再分发给线程池里的子事件循环处理。
    EventLoop* baseLoop_;
    std::string name_;

    //线程池的启动标识
    bool started_;
    //创建的`EventLoopThread`数量
    int numThreads_;
    //轮询分配的原子计数器，是 Round-Robin 负载均衡的核心。
    //每次调用`getNextLoop()`时，计数器原子自增，再对线程总数取模得到对应子事件循环的索引
    std::atomic<size_t> next_{ 0 };
    //管理所有事件循环线程对象的生命周期。
    //每个元素持有一个`EventLoopThread`的独占智能指针，线程池销毁时会自动逐个释放所有线程对象，触发线程停止和资源回收，避免内存泄漏。
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    //存储所有子事件循环的裸指针，用于快速访问
    std::vector<EventLoop*> loops_;
};
