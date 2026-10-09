#include "Acceptor.hpp"

#include <arpa/inet.h>  // inet_ntop / INET_ADDRSTRLEN / htons / htonl / ntohs
#include <cerrno>       // errno / EAGAIN / EWOULDBLOCK / EMFILE / ENFILE
#include <cstdio>       // perror
#include <fcntl.h>      // open / O_RDONLY / O_CLOEXEC
#include <netinet/in.h> // struct sockaddr_in / INADDR_ANY / IPPROTO_TCP
#include <sys/socket.h> // socket / bind / listen / accept / accept4 / setsockopt
#include <unistd.h>     // close / _exit

// 初始化列表顺序与成员声明顺序保持一致
Acceptor::Acceptor(EventLoop* loop, uint16_t port)
	: listenFd(createListenFd(port)),
	  idleFd(::open("/dev/null", O_RDONLY | O_CLOEXEC)),
	  loop(loop),
	  ch(listenFd) {
	ch.setReadCallback([this] {handRead();});
}

Acceptor::~Acceptor() {
	close(listenFd);
	if (idleFd >= 0)::close(idleFd);
}

//拿到连接后交付上层
void Acceptor::setCallback(NewConnectionCallback cb) {
	cb_ = cb;
}

//创建socket并且设置监听
int Acceptor::createListenFd(uint16_t port) {
	int lfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
	int opt = 1;
	::setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));//端口复用
	struct sockaddr_in a {};
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	a.sin_port = htons(port);
	if (::bind(lfd, (struct sockaddr*)&a, sizeof(a)) < 0) { perror("bind"); _exit(1); }
	if (::listen(lfd, 128) < 0) { perror("listen"); _exit(1); }
	return lfd;
}

//注册到对应的epoll内核
void Acceptor::listen() {
	ch.enableRead();
	loop->updateChannel(&ch);
}

//处理连接逻辑，用于注入channel
//每次取出一个连接，循环取出，可以兼容边缘触发
void Acceptor::handRead() {
	while (true) {
		struct sockaddr_in peer {};
		socklen_t len = sizeof(peer);
		int cfd = ::accept4(listenFd, reinterpret_cast<struct sockaddr*>(&peer), &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
		if (cfd >= 0) {
			char ip[INET_ADDRSTRLEN] = { 0 };
			::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
			if (cb_) cb_(cfd, ip, ntohs(peer.sin_port)); // 交付上层,交付的是人类可读的ip字符串，以及小端端口号
		}
		else {
			if (errno == EAGAIN || errno == EWOULDBLOCK)break;//连接取完了直接退出循环即可
			if (errno == EMFILE || errno == ENFILE) {
				//先关闭占位的空白fd，然后让新连接连接使用这个fd，然后马上关闭这个fd，相当于拒绝客户端的连接
				//然后再次占用这个空白fd，这样可以避免epoll不断触发造成忙等
				::close(idleFd);
				int fd = ::accept(listenFd, nullptr, nullptr);
				if (fd >= 0) ::close(fd);
				idleFd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
				break;
			}
			perror("accept4");break;
		}
	}
}
