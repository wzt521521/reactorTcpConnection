#include "EventLoop.hpp"

#include <cerrno>   // errno / EINTR
#include <cstdio>   // perror

// 静态整型常量的类外定义，避免低标准下ODR-use的链接错误
constexpr size_t EventLoop::maxLenOfevent;

EventLoop::EventLoop() {
	epollfd = epoll_create1(EPOLL_CLOEXEC);
	vecOfevent.resize(maxLenOfevent);
}

EventLoop::~EventLoop() {
	close(epollfd);
}

void EventLoop::Loops() {
	while (true) {
		int n = epoll_wait(epollfd, vecOfevent.data(),
		                   static_cast<int>(vecOfevent.size()), -1);
		if (n < 0) {
			if (errno == EINTR)continue;
			perror("epoll_wait");
			break;
		}
		for (int i = 0; i < n; i++) {
			// 事件带回的ptr就是注册时预埋的Channel指针
			static_cast<Channel*>(vecOfevent[i].data.ptr)
				->handleEvent(vecOfevent[i].events);
		}
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
