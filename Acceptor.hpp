#pragma once

#include <cstdint>      // uint16_t
#include <functional>   // std::function
#include <string>       // std::string

#include "EventLoop.hpp"  // EventLoop / Channel（EventLoop.hpp已包含Channel.hpp）

// TCP连接接收器：监听端口，accept新连接并通过回调交付上层
class Acceptor {
public:
	using NewConnectionCallback = std::function<void(int, const std::string&, uint16_t)>;//fd,ip,端口

	Acceptor(EventLoop* loop, uint16_t port);
	~Acceptor();

	//拿到连接后交付上层
	void setCallback(NewConnectionCallback cb);

	//创建socket并且设置监听，注册到对应的epoll内核
	void listen();

private:
	//创建监听socket并完成bind/listen
	static int createListenFd(uint16_t port);

	//处理监听fd的可读事件：循环取出连接，取到EAGAIN为止，可兼容边缘触发
	void handRead();

	int listenFd;
	int idleFd;           //预留一个空闲fd，fd耗尽时应急腾位
	EventLoop* loop;
	Channel ch;           //监听fd对应的Channel
	NewConnectionCallback cb_;
};
