#include "Packet.hpp"

#include <arpa/inet.h> // htons / htonl

#include "Buffer.hpp"

void Packet::setFrameCallback(FrameCallback cb) {
    frameCallback_ = std::move(cb);
}

//解码：从buf循环切出完整帧，半包留在buf中等待后续数据
bool Packet::decode(const Tcpconnection::Ptr& conn, Buffer* buf) {
    while (buf->getReadable() >= 4) {
        //先获得首部4字节总长度
        uint32_t total = buf->peekInt32();
        //合法性校验：总长度小于头部长度，或超过上限，直接判非法
        if (total < KHeaderLen || total > kMaxPacket) {
            return false;
        }
        if (buf->getReadable() < total) break; //半包则退出循环，等待数据到齐
        uint16_t cmd = buf->peekInt16();
        //先拷贝出body，再消费整帧，顺序不能反
        std::string body(buf->peekRead() + KHeaderLen, total - KHeaderLen);
        buf->retrieve(total);
        if (frameCallback_) frameCallback_(conn,cmd, body);
    }
    return true;
}

//编码：把cmd和body按帧格式写入buf
void Packet::encode(Buffer* buf, uint16_t cmd, const std::string& body) {
    uint32_t total = static_cast<uint32_t>(KHeaderLen)
                   + static_cast<uint32_t>(body.size());
    // 1. 先写包体
    buf->append(body.size(), body.data());
    // 2. 前置2字节命令字：用独立的2字节变量承载网络序值
    uint16_t cmdNet = htons(cmd);
    buf->prepend(2, reinterpret_cast<const char*>(&cmdNet));
    // 3. 前置4字节总长度：用独立的4字节变量承载网络序值
    uint32_t lenNet = htonl(total);
    buf->prepend(4, reinterpret_cast<const char*>(&lenNet));
}
