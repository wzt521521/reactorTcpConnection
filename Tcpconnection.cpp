#include "Tcpconnection.hpp"

#include <cerrno>       // errno / EAGAIN / EWOULDBLOCK
#include <cstring>      // strerror
#include <iostream>     // std::cerr / std::endl
#include <sys/socket.h> // send / getsockopt / SOL_SOCKET / SO_ERROR / MSG_NOSIGNAL / socklen_t
#include <unistd.h>     // ssize_t / close
#include <utility>      // std::move

#include "Packet.hpp"   // Packet::encode

Tcpconnection::Tcpconnection(EventLoop* loop, int fd, const std::string& ip, uint16_t port)
	: loop_(loop), fd(fd), ip_(ip), port_(port),
	state(kConnecting), ch(fd) {
	// 绑定四类事件回调
	ch.setReadCallback([this] { handleRead(); });
	ch.setWriteCallback([this] { handleWrite(); });
	ch.setCloseCallback([this] { handleClose(); });
	ch.setErrorCallback([this] { handleError(); });
}
Tcpconnection::~Tcpconnection(){
	close(fd);
}
int Tcpconnection::getFd() {
	return fd;
}

//注入上层回调
void Tcpconnection::setCloseCallback(CloseCallback cb) {
	closeCallback_ = std::move(cb);
}

void Tcpconnection::setMessageCallback(MessageCallback cb) {
	messageCallback_ = std::move(cb);
}

//连接建立完成：切状态、注册读事件
void Tcpconnection::connectionSuccess() {
	ch.tie(shared_from_this());
	state = kConnected;
	ch.enableRead();
	loop_->updateChannel(&ch);
}

//这个指把body尝试直接发送到客户端
void Tcpconnection::sendPacket(uint16_t cmd, const std::string& body) {
	if (state != kConnected)return;
	//经过encode buffer里面已经有完整的协议了
	Packet::encode(&OutputBuffer, cmd, body);
	//尝试直接发送
	if (!ch.isWriting()) {
		ssize_t n = send(fd, OutputBuffer.peekRead(), OutputBuffer.getReadable(), MSG_NOSIGNAL);
		if (n >= 0) {
			//说明已经成功的发送了部分到缓冲区
			OutputBuffer.retrieve(n);
			if (OutputBuffer.getReadable() == 0) {
				//说明发完了
				return;//无需开启写事件
			}
		}
		else {
			//n<0则处理错误
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				handleClose();
				return;
			}
		}
		// 走到这里说明没有发完
		ch.enableWrite();
		loop_->updateChannel(&ch);
	}

	//注意，这个并没有while死循环，也就是只会尝试发送一次，如果没有发完，就会剩下到buffer里面，交给handlewrite处理
}

//强制关闭连接
void Tcpconnection::forceClose() {
	handleClose();
}

//指从socket进行读取然后装填到buffer
void Tcpconnection::handleRead() {
	int err = 0;
	while (true) {
		//buffer提供的从缓冲区读取的函数并不会保证读完，所以要循环处理
		ssize_t n = InputBuffer.readFromSocket(fd, &err);
		if (n > 0) {
			//说明成功读取到了部分数据
			//不进行任何业务处理，只是负责把数据从内核缓冲区读取到业务缓冲区buffer
			//调用上层回调函数，交由上层处理
			messageCallback_(shared_from_this(), &InputBuffer);//在当前main注入逻辑中，此时是调用decode
		}
		else if (n == 0) {
			//对端已经发送完了，可以关闭连接
			handleClose();
			return;
		}
		else {
			if (err == EAGAIN || err == EWOULDBLOCK) break;//如果是半包则跳出即可，否则进入下面的错误处理
			handleClose();return;
		}
	}

}

//指从输出缓冲区取出数据发送
void Tcpconnection::handleWrite() {
	if (OutputBuffer.getReadable() == 0)return;
	ssize_t n = ::send(fd, OutputBuffer.peekRead(), OutputBuffer.getReadable(), MSG_NOSIGNAL);
	if (n >= 0) {
		OutputBuffer.retrieve(n);
		if (OutputBuffer.getReadable() == 0) {
			ch.disableWrite();
			loop_->updateChannel(&ch);
		}
	}
	else if (errno != EAGAIN && errno != EWOULDBLOCK) {
		handleClose(); // 写错误，关闭连接
	}
}

//关闭连接调用入口，保证幂等性
void Tcpconnection::handleClose() {
	if (state == kDisconnected) return;
	state = kDisconnected;
	ch.disableAll();
	loop_->removeChannel(&ch); // 先从epoll移除,此时channel对象还在，只不过没有被注册到epoll了
	if (closeCallback_) closeCallback_(shared_from_this()); // 再通知上层销毁
}

//错误事件处理
void Tcpconnection::handleError() {
	int err = 0; socklen_t len = sizeof(err);
	::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
	std::cerr << "[Conn] EPOLLERR: " << strerror(err) << std::endl;
	handleClose();
}
