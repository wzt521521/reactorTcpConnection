#pragma once

#include <cstdint>      // uint32_t
#include <functional>   // std::function
#include <sys/epoll.h>  // EPOLLIN / EPOLLOUT / EPOLLHUP 等事件常量

// 一个fd对应一个Channel，保存该fd关注的事件掩码和事件触发后的回调
class Channel {
public:
	using EventCallback = std::function<void()>;

	explicit Channel(int fd);

	int getFd() const;
	uint32_t getEvents() const;

	// 事件开关：位运算修改关注的事件掩码
	void enableRead();
	void enableWrite();
	void disableWrite();
	void disableAll();

	// 设置四类事件回调
	void setReadCallback(EventCallback cb);
	void setWriteCallback(EventCallback cb);
	void setCloseCallback(EventCallback cb);
	void setErrorCallback(EventCallback cb);

	// epoll通知事件到达时的分发入口，revents为实际触发的事件掩码
	void handleEvent(uint32_t revents);

private:
	int fd;
	uint32_t events;
	EventCallback readCallback;
	EventCallback writeCallback;
	EventCallback closeCallback;
	EventCallback errorCallback;
};
