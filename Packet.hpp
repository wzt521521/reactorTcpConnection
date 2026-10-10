#pragma once

#include <cstddef>    // size_t
#include <cstdint>    // uint16_t / uint32_t
#include <functional> // std::function
#include <string>     // std::string

#include "Buffer.hpp"
#include "Tcpconnection.hpp"  // Tcpconnection::Ptr

// 协议帧编解码
// 帧格式：[4字节总长度(网络序) | 2字节cmd(网络序) | body]
class Packet {
public:
    static constexpr size_t KHeaderLen = 6;
    static constexpr uint32_t kMaxPacket = 10u * 1024 * 1024;

    //帧回调：交付解析出的cmd和body
    using FrameCallback = std::function<void(const Tcpconnection::Ptr& conn, uint16_t cmd, const std::string& body)>;

    //注入帧回调
    void setFrameCallback(FrameCallback cb);

    //解码：从buf循环切出完整帧，半包留在buf中等待后续数据
    //返回值：true 正常（含半包等待）；false 遇到非法帧，上层应断开连接
    bool decode(const Tcpconnection::Ptr& conn, Buffer* buf);

    //编码：把cmd和body按帧格式写入buf（静态函数，不依赖成员状态）
    static void encode(Buffer* buf, uint16_t cmd, const std::string& body);

private:
    //上层注入的帧回调
    FrameCallback frameCallback_;
};
