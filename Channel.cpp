#include "Channel.hpp"

#include <utility>  // std::move

Channel::Channel(int fd) : fd(fd), events(0) {}

int Channel::getFd() const {
	return fd;
}

uint32_t Channel::getEvents() const {
	return events;
}

// 关注可读事件，EPOLLRDHUP表示对端关闭连接
void Channel::enableRead() {
	events |= (EPOLLIN | EPOLLPRI | EPOLLRDHUP);
}

void Channel::enableWrite() {
	events |= EPOLLOUT;
}

void Channel::disableWrite() {
	events &= ~EPOLLOUT;
}

void Channel::disableAll() {
	events = 0;
}

bool Channel::isWriting() const {
	return (events & EPOLLOUT) != 0;
}

void Channel::setReadCallback(EventCallback cb) {
	readCallback = std::move(cb);
}

void Channel::setWriteCallback(EventCallback cb) {
	writeCallback = std::move(cb);
}

void Channel::setCloseCallback(EventCallback cb) {
	closeCallback = std::move(cb);
}

void Channel::setErrorCallback(EventCallback cb) {
	errorCallback = std::move(cb);
}

// 绑定持有者对象，弱引用不增加引用计数
void Channel::tie(const std::shared_ptr<void>& obj) {
	tie_ = obj;
	tied_ = true;
}

void Channel::handleEvent(uint32_t revents) {
	std::shared_ptr<void> guard; // 栈上强引用，为持有者对象续命
	if (tied_) {
		guard = tie_.lock(); // 弱引用升级为强引用
		if (!guard) return;  // 升级失败说明持有者已销毁，丢弃事件
	}
	// 对端挂断且没有剩余可读数据，通知上层关闭
	if ((revents & EPOLLHUP) && !(revents & EPOLLIN) && closeCallback) {
		closeCallback();
	}
	// 错误事件
	if ((revents & EPOLLERR) && errorCallback) {
		errorCallback();
	}
	// 可读事件（含带外数据）
	if ((revents & (EPOLLIN | EPOLLPRI)) && readCallback) {
		readCallback();
	}
	// 可写事件
	if ((revents & EPOLLOUT) && writeCallback) {
		writeCallback();
	}
}
