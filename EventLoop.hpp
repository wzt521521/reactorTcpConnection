#pragma once

#include <sys/epoll.h>      // epoll_create1 / epoll_wait / epoll_ctl / epoll_event
#include <unistd.h>         // close
#include <atomic>           // std::atomic
#include <cstddef>          // size_t
#include <functional>       // std::function
#include <mutex>            // std::mutex
#include <thread>           // std::thread
#include <vector>
#include <unordered_map>

#include "Channel.hpp"

// epoll事件循环：持有epoll实例，管理所有注册的Channel，阻塞等待并分发事件
class EventLoop {
public:
	using Functor = std::function<void()>;
	EventLoop();
	~EventLoop();

	// 事件循环主函数
	void Loops();

	// 把Channel注册到epoll，已存在则修改关注事件
	void updateChannel(Channel* chan);

	// 把Channel从epoll移除，同时从映射表删除
	void removeChannel(Channel* ch);

	bool isInLoopThread() const;
	void quit();

    void queueInLoop(Functor cb);
    void wakeup();

private:
	// 创建 eventfd，用于跨线程唤醒当前 EventLoop
    static int createEventfd();
    // wakeupFd_ 的可读回调：读空 eventfd，防止 LT 模式下反复触发
    void handleWakeup();
    // 取出 pendingFunctors_ 中的任务并在本 loop 线程执行
    void doPendingFunctors();


    static constexpr size_t maxLenOfevent = 16;
    int epollfd;     // epoll 实例：负责等待事件
	int wakeupFd_;   // eventfd：负责让其他线程主动唤醒这个 epoll
    std::thread::id tid_;
    Channel wakeupChannel_;
    std::mutex mtx_;
    // 任务队列
    std::vector<Functor> pendingFunctors_;
    // 当前是否正在执行排队任务；用于判断本线程新投递任务时是否需要唤醒
    std::atomic<bool> callingPendingFunctors_{false};
    // EventLoop 是否退出的标志
    std::atomic<bool> quit_{false};
    std::vector<struct epoll_event> vecOfevent;
    std::unordered_map<int, Channel*> channels_;


};
