#include "EventLoopThreadPool.hpp"

#include "EventLoop.hpp"
#include "EventLoopThread.hpp"

#include <utility>

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop, std::string name)
    : baseLoop_(baseLoop),
    name_(std::move(name)),
    started_(false),
    numThreads_(0) {
}
//设置线程池的数量
void EventLoopThreadPool::setThreadNum(int numThreads) {
    if (!started_) {
        numThreads_ = numThreads;
    }
}
//开启线程池并且创建线程
void EventLoopThreadPool::start(ThreadInitCallback cb) {
    if (started_) {
        return;
    }

    started_ = true;

    for (int i = 0; i < numThreads_; ++i) {
        //unique_ptr
        auto thread = std::make_unique<EventLoopThread>(
            cb, name_ + std::to_string(i));
        //更新线程池的核心数组，保存线程池的状态
        loops_.push_back(thread->startLoop());
        threads_.push_back(std::move(thread));// unique_ptr不可拷贝，必须移动语义转入容器
    }

    if (numThreads_ == 0 && cb) {
        cb(baseLoop_);
    }
}

EventLoop* EventLoopThreadPool::getNextLoop() {
    //如果线程池里没有创建任何 IO 线程，就返回 `baseLoop_`。
    //也就是说，线程数为 0 时，所有工作仍由 base loop 处理。
    if (loops_.empty()) {
        return baseLoop_;
    }

    //不执行任务，也不判断线程当前忙不忙
    //简单的让每个线程轮流返回
    size_t index = next_.fetch_add(1) % loops_.size();
    return loops_[index];
}
//返回loop_数组
const std::vector<EventLoop*>& EventLoopThreadPool::getLoops() const {
    return loops_;
}