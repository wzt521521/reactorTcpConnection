#pragma once

#include <sys/epoll.h>      // epoll_create1 / epoll_wait / epoll_ctl / epoll_event
#include <unistd.h>         // close
#include <cstddef>          // size_t
#include <vector>
#include <unordered_map>

#include "Channel.hpp"

// epoll事件循环：持有epoll实例，管理所有注册的Channel，阻塞等待并分发事件
class EventLoop {
public:
	EventLoop();
	~EventLoop();

	// 事件循环主函数
	void Loops();

	// 把Channel注册到epoll，已存在则修改关注事件
	void updateChannel(Channel* chan);

	// 把Channel从epoll移除，同时从映射表删除
	void removeChannel(Channel* ch);

private:
	static constexpr size_t maxLenOfevent = 16;  // 单轮epoll_wait最多返回的事件数
	int epollfd;
	std::vector<struct epoll_event> vecOfevent;
	std::unordered_map<int, Channel*> channels_;  // fd到Channel的映射
};
