#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class EventLoop;

class EventLoopThread {
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    EventLoopThread(ThreadInitCallback cb = ThreadInitCallback(),
        std::string name = std::string());
    ~EventLoopThread();

    EventLoop* startLoop();

private:
    void threadFunc();

    EventLoop* loop_;//快速访问，loop对象由子线程的栈持有，线程类实例持有线程，线程的栈持有eventloop，这里的线程类的loop指针仅仅作为快速访问
    std::thread thread_;
    std::mutex mtx_;
    std::condition_variable cv_;
    ThreadInitCallback initCallback_;
    std::string name_;
};