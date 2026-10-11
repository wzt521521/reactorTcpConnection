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

    EventLoop* loop_;
    std::thread thread_;
    std::mutex mtx_;
    std::condition_variable cv_;
    ThreadInitCallback initCallback_;
    std::string name_;
};