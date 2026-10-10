#pragma once

#include <cstdint>  // uint16_t

// 业务命令字定义
constexpr uint16_t kCmdEcho   = 1;      // 回声
constexpr uint16_t kCmdLogin  = 2;      // 登录
constexpr uint16_t kCmdChat   = 3;      // 聊天
constexpr uint16_t kCmdNotice = 4;      // 服务器公告
constexpr uint16_t kCmdError  = 0xFFFF; // 错误
