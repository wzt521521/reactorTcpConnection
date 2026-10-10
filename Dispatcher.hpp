#pragma once

#include <cstdint>      // uint16_t
#include <functional>   // std::function
#include <string>
#include <unordered_map>
#include <utility>      // std::move

#include "Commands.hpp"
#include "Tcpconnection.hpp"  // Tcpconnection::Ptr / sendPacket

// 消息分发器：按命令字查表路由到对应处理函数
// 新增业务命令只需 registerHandler，不用改动底层
class MessageDispatcher {
public:
	using CommandHandler = std::function<void(const Tcpconnection::Ptr&, uint16_t, const std::string&)>;

	// 注册指定命令的处理函数
	void registerHandler(uint16_t cmd, CommandHandler h) {
		handlers_[cmd] = std::move(h);
	}

	// 分发消息：查到就调用对应处理函数，查不到回错误帧
	void dispatch(const Tcpconnection::Ptr& conn, uint16_t cmd, const std::string& body) {
		auto it = handlers_.find(cmd);
		if (it != handlers_.end())
			it->second(conn, cmd, body);
		else
			conn->sendPacket(kCmdError, "unknown command");
	}

private:
	std::unordered_map<uint16_t, CommandHandler> handlers_; // 命令号 -> 处理函数
};
