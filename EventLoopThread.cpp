#include "EventLoopThread.hpp"

#include "EventLoop.hpp"

EventLoopThread::EventLoopThread(ThreadInitCallback cb, std::string name)
    : loop_(nullptr),
    initCallback_(std::move(cb)),
    name_(std::move(name)) {
}

EventLoopThread::~EventLoopThread() {
    if (loop_) {
        loop_->quit();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

//startLoop只会由主线程调用
EventLoop* EventLoopThread::startLoop() {
    //创建子线程并且执行对应的任务函数
    thread_ = std::thread([this] {
        threadFunc();
        });
    EventLoop* result = nullptr;
    {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] {
            return loop_ != nullptr;
            });
        result = loop_;//让result指向子线程创建的loop资源
    }

    return result;
}

//子线程执行函数
void EventLoopThread::threadFunc() {
    EventLoop loop;//创建子线程自己的栈上EventLoop，负责处理线程自己的连接，主线程只负责监听等核心操作
    //调用子线程初始化函数
    if (initCallback_) {
        //当前尚未知晓回调函数的设计细节
        initCallback_(&loop);//传入子线程自己的栈对象loop的地址
    }

    {
        std::lock_guard<std::mutex> lock(mtx_);
        loop_ = &loop;//这里把子线程自己的栈上创建的loop对象赋值给了主线程的loop_
    }
    cv_.notify_one();//唤醒主线程，这个cv_是主线程的资源

    loop.Loops();//执行子线程自己的loop函数循环，负责自己的监听

    std::lock_guard<std::mutex> lock(mtx_);//遵循一个原则，操作共享变量前上锁
    loop_ = nullptr;
}