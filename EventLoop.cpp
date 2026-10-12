#include "EventLoop.hpp"

#include <cerrno>        // errno / EINTR
#include <cstdint>       // uint64_t
#include <cstdio>        // perror
#include <sys/eventfd.h> // eventfd
#include <unistd.h>      // read / write / close
#include <utility>       // std::move

// 静态整型常量的类外定义，避免低标准下ODR-use的链接错误
constexpr size_t EventLoop::maxLenOfevent;

// 子线程没有网络事件时会阻塞在 epoll_wait。
// 把 eventfd 封装成 Channel 注册到当前 epoll，作为跨线程通知事件。
// 其他线程写入 eventfd 后，eventfd 可读，epoll_wait 随即返回。
// 这样跨线程唤醒也复用 Channel/epoll 分发机制，和网络 IO 的处理方式统一。
EventLoop::EventLoop()
	: epollfd(::epoll_create1(EPOLL_CLOEXEC)),
	  wakeupFd_(createEventfd()),
	  tid_(std::this_thread::get_id()),
	  wakeupChannel_(wakeupFd_) {
	vecOfevent.resize(maxLenOfevent);

	wakeupChannel_.setReadCallback([this] {
		handleWakeup();
	});
	wakeupChannel_.enableRead();
	updateChannel(&wakeupChannel_);
}

EventLoop::~EventLoop() {
	::close(wakeupFd_);
	::close(epollfd);
}

//一共3种类型事件，客户端发数据socket fd，其他线程投递任务eventfd，定时器到期timerfd
//先调用每个事件的handleEvent
//然后调用doPendingFunctors()让子线程处理任务
void EventLoop::Loops() {
	while (!quit_.load()) {
		int n = epoll_wait(epollfd, vecOfevent.data(),
		                   static_cast<int>(vecOfevent.size()), -1);
		if (n < 0) {
			if (errno == EINTR)continue;
			perror("epoll_wait");
			break;
		}
		for (int i = 0; i < n; i++) {
			// 事件带回的ptr就是注册时updateChannel预埋的Channel指针
			static_cast<Channel*>(vecOfevent[i].data.ptr)
				->handleEvent(vecOfevent[i].events);
		}

		doPendingFunctors();
	}
}

void EventLoop::updateChannel(Channel* chan) {
	struct epoll_event ev{};
	ev.events = chan->getEvents();
	ev.data.ptr = chan;
	int fd = chan->getFd();
	if (channels_.count(fd)) {
		epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev);  // 已注册：修改
	} else {
		epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);  // 未注册：新增
		channels_[fd] = chan;
	}
}

void EventLoop::removeChannel(Channel* ch) {
	epoll_ctl(epollfd, EPOLL_CTL_DEL, ch->getFd(), nullptr);//从epoll内核中删除
	channels_.erase(ch->getFd());//从map中删除
}

bool EventLoop::isInLoopThread() const {
	return tid_ == std::this_thread::get_id();
}

void EventLoop::quit() {
	quit_.store(true);

	if (!isInLoopThread()) {
		wakeup();
	}
}


void EventLoop::queueInLoop(Functor cb) {
	bool needWake = false;

	{
		std::lock_guard<std::mutex> lock(mtx_);

		needWake = (!isInLoopThread()) ||
		           callingPendingFunctors_.load();

		pendingFunctors_.push_back(std::move(cb));
	}

	//第一种情况，子线程没有执行任务，那么无需唤醒，loop函数一定会调用doPendingFunctors();
	//第二种情况，子线程正在执行任务，此时functors.swap(pendingFunctors_)导致原来的数组为空
	//如果不调用wakeup，等其他线程的loop函数来调用doPendingFunctors()，那么会导致新任务被放到旧的任务数组，导致这个新任务不被发现
	if (needWake) {
		wakeup();
	}
}

void EventLoop::wakeup() {
	uint64_t one = 1;
	ssize_t n = ::write(wakeupFd_, &one, sizeof(one));

	if (n != static_cast<ssize_t>(sizeof(one))) {
		if (errno != EAGAIN) {
			perror("write eventfd");
		}
	}
}

//创建一个事件通知的文件描述符eventfd，子线程把它注册进子线程自己的 epoll，让主线程写eventfd
//它一变可读，子线程的 `epoll_wait()` 就返回
int EventLoop::createEventfd() {
	int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

	if (fd < 0) {
		perror("eventfd");
	}

	return fd;
}

//被注入handleEvent，负责读取事件，避免事件一直触发
//这个处理事件只是为了防止一直触发epoll，而非真的处理了事件任务
void EventLoop::handleWakeup() {
	uint64_t value = 0;
	ssize_t n = ::read(wakeupFd_, &value, sizeof(value));

	if (n != static_cast<ssize_t>(sizeof(value))) {
		perror("read eventfd");
	}
}

//事件的任务被放在线程的任务数组里面
//通过此函数从数组里面拿出任务，并且上锁，准备执行任务
void EventLoop::doPendingFunctors() {
	std::vector<Functor> functors;

	callingPendingFunctors_.store(true);

	{
		std::lock_guard<std::mutex> lock(mtx_);
		functors.swap(pendingFunctors_);
	}

	for (const Functor& cb : functors) {
		cb();
	}

	callingPendingFunctors_.store(false);
}
