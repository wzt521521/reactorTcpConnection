#include "Buffer.hpp"

#include <algorithm>   // std::copy
#include <arpa/inet.h> // ntohl / ntohs
#include <cerrno>      // errno
#include <cstring>     // std::memcpy
#include <iostream>    // std::cout / std::endl
#include <sys/uio.h>   // struct iovec / readv
#include <unistd.h>    // readv

// 静态整型常量的类外定义，避免ODR-use时的链接错误
const size_t Buffer::kCheapPrepend;
const size_t Buffer::kInitialSize;

Buffer::Buffer() {
    buffer_.resize(kInitialSize + kCheapPrepend);
    readIndex = kCheapPrepend;
    writeIndex = kCheapPrepend;
}

//获取读指针的最新位置
const char* Buffer::peekRead() const {
    return &buffer_[0] + readIndex;
}

char* Buffer::peekRead() {
    return &buffer_[0] + readIndex;
}

//获取写指针的最新位置
const char* Buffer::peekWrite() const {
    return &buffer_[0] + writeIndex;
}

char* Buffer::peekWrite() {
    return &buffer_[0] + writeIndex;
}

//获取读指针前面有多少空间（含8字节预留区）
size_t Buffer::getPreRead() const {
    return readIndex;
}

//获取有多少写入空间
size_t Buffer::getWriteable() const {
    return buffer_.size() - writeIndex;
}

//获取有多少可读的内容
size_t Buffer::getReadable() const {
    return writeIndex - readIndex;
}

//从可读区起始处读取4字节网络序整数（总长度字段）
uint32_t Buffer::peekInt32() const {
    uint32_t x;
    std::memcpy(&x, peekRead(), 4);
    return ntohl(x);
}

//从可读区起始处偏移4字节读取2字节网络序整数（cmd字段）
uint16_t Buffer::peekInt16() const {
    uint16_t x;
    std::memcpy(&x, peekRead() + 4, 2);
    return ntohs(x);
}

//前置写入
void Buffer::prepend(size_t len, const char* data) {
    //先判断大小是否足够
    if (len > getPreRead()) {
        std::cout << "前置空间不足写入失败" << std::endl;
        return;
    }
    readIndex -= len;
    std::copy(data, data + len, peekRead()); //左闭右开，第3个参数为写入起始位置
}

//顺序写入
void Buffer::append(size_t len, const char* data) {
    //先判断大小是否足够
    if (len > getWriteable()) {
        //全加法判据：前移后可用空间 = writable + readIndex - kCheapPrepend
        if (getWriteable() + readIndex < len + kCheapPrepend) {
            //说明仍旧不够，需要扩容
            makeSpace(len - getWriteable());
        } else {
            //说明可以通过数据前移来避免扩容
            DataToFront();
        }
        //扩容后写入缓冲区
        std::copy(data, data + len, peekWrite());
    } else {
        //空间足够
        std::copy(data, data + len, peekWrite());
    }
    writeIndex += len;
}

//扩容操作,函数设计意图为直接扩大len的长度
void Buffer::makeSpace(size_t len) {
    buffer_.resize(buffer_.size() + len);
}

//数据前移
void Buffer::DataToFront() {
    size_t p = writeIndex - readIndex;
    std::copy(&buffer_[readIndex], &buffer_[readIndex] + p,
              &buffer_[kCheapPrepend]);
    readIndex = kCheapPrepend;
    writeIndex = readIndex + p;
}

//提供从tcp缓冲区读入数据到buffer_的功能
ssize_t Buffer::readFromSocket(int fd, int* saveErrno) {
    char extrabuf[65536]; //栈上的应急缓冲区
    struct iovec vec[2];
    size_t w = getWriteable();
    vec[0].iov_base = &buffer_[0] + writeIndex;
    vec[0].iov_len = w;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);
    ssize_t n = ::readv(fd, vec, w < sizeof(extrabuf) ? 2 : 1);
    if (n < 0) {
        *saveErrno = errno;
    } else if (static_cast<size_t>(n) <= w) {
        writeIndex += static_cast<size_t>(n); //数据全落在Buffer内，只移动写指针
    } else {
        writeIndex = buffer_.size();          //第一块写满
        append(static_cast<size_t>(n) - w, extrabuf); //溢出部分追加，触发扩容
    }
    return n;
}

void Buffer::retrieve(size_t len) {
    if (len < getReadable()) readIndex += len;
    else readIndex = writeIndex = kCheapPrepend;
}
