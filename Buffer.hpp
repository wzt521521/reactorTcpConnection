#pragma once

#include <cstddef>    // size_t
#include <cstdint>    // uint32_t / uint16_t
#include <unistd.h>   // ssize_t
#include <vector>

// 应用层接收/发送缓冲区
// 内存布局：[头部预留区 | 待读数据区 | 可写空间区]
class Buffer {
public:
    static const size_t kCheapPrepend = 8;   // 头部预留8字节，供prepend封装协议头
    static const size_t kInitialSize = 1024; // 初始可写容量

    Buffer();

    //获取读指针的最新位置
    const char* peekRead() const;
    char* peekRead();

    //获取写指针的最新位置
    const char* peekWrite() const;
    char* peekWrite();

    //获取读指针前面有多少空间（含8字节预留区）
    size_t getPreRead() const;
    //获取有多少写入空间
    size_t getWriteable() const;
    //获取有多少可读的内容
    size_t getReadable() const;

    //从可读区起始处读取4字节网络序整数（总长度字段）
    uint32_t peekInt32() const;
    //从可读区起始处偏移4字节读取2字节网络序整数（cmd字段）
    uint16_t peekInt16() const;

    //前置写入（封装协议头用）
    void prepend(size_t len, const char* data);
    //顺序写入，空间不足时自动前移或扩容
    void append(size_t len, const char* data);
    //扩容
    void makeSpace(size_t len);
    //将未读数据整体前移到预留区末尾
    void DataToFront();

    //从fd批量读入数据
    //返回值：>0 读到的字节数；0 对端关闭(FIN)；<0 出错，错误码在saveErrno中
    ssize_t readFromSocket(int fd, int* saveErrno);
    //消耗len字节：len<可读量时移动读指针，否则双指针复位到预留区末尾
    void retrieve(size_t len);

private:
    std::vector<char> buffer_;
    size_t readIndex;  //读指针偏移量,指向第一个未读字节
    size_t writeIndex; //写指针偏移量,指向第一个可写位置
};
