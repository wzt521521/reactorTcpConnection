#pragma once

#include <cstdint>      // uint16_t
#include <functional>   // std::function
#include <memory>       // std::shared_ptr / std::enable_shared_from_this
#include <string>       // std::string

#include "EventLoop.hpp"  // EventLoop / Channel
#include "Buffer.hpp"     // Buffer 值成员需要完整定义

enum StateE { kConnecting, kConnected, kDisconnecting, kDisconnected };
class Tcpconnection : public std::enable_shared_from_this<Tcpconnection> {
public:
	using Ptr = std::shared_ptr<Tcpconnection>;
	using CloseCallback = std::function<void(const Ptr&)>;
	using MessageCallback = std::function<void(const Ptr&, Buffer*)>;

	Tcpconnection(EventLoop* loop, int fd, const std::string& ip, uint16_t port);
	~Tcpconnection();
	int getFd();

	//注入上层回调
	void setCloseCallback(CloseCallback cb);
	void setMessageCallback(MessageCallback cb);

	//业务处理
	void setContext(std::shared_ptr<void> c) {
		context_ = std::move(c);
	}
	template<typename T>
	std::shared_ptr<T> getContext() const {
		return std::static_pointer_cast<T>(context_);
	}

	//连接建立完成：切状态、注册读事件
	void connectionSuccess();

	//把body按协议封帧后尝试发送，发不完的留在输出缓冲区等写事件
	void sendPacket(uint16_t cmd, const std::string& body);

	//强制关闭连接
	void forceClose();

private:
	//指从socket进行读取然后装填到buffer
	void handleRead();
	//指从输出缓冲区取出数据发送
	void handleWrite();
	//关闭连接调用入口，保证幂等性
	void handleClose();
	//错误事件处理
	void handleError();

	EventLoop* loop_;
	int fd;
	std::string ip_;
	uint16_t port_;
	StateE state;
	Channel ch;
	Buffer InputBuffer;
	Buffer OutputBuffer;
	CloseCallback closeCallback_;
	MessageCallback messageCallback_;
	std::shared_ptr<void> context_; // 类型擦除的业务会话上下文
};
