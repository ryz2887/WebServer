#ifndef BUFFER_H//头文件保护
#define BUFFER_H
#include <cstring>
#include <iostream>
#include <unistd.h>//linux调用
#include <sys/uio.h>//读写操作
#include <vector>
#include <atomic>
#include <assert.h>//调试用
class Buffer {
public:
    Buffer(int initBuffSize = 1024);
    ~Buffer() = default;

    size_t WritableBytes() const;//无符号整数
    size_t ReadableBytes() const;
    size_t PrependableBytes() const;

    const char* Peek() const;
    void EnsureWriteable(size_t len);
    void HasWritten(size_t len);

    void Retrieve(size_t len);
    void RetrieveUntil(const char* end);

    void RetrieveAll();
    std::string RetrieveAllToStr();

    const char* BeginWriteConst() const;
    char* BeginWrite();

    void Append(const std::string& str);
    void Append(const char* str, size_t len);
    void Append(const void* data, size_t len);//万能指针
    void Append(const Buffer& buff);

    ssize_t ReadFd(int fd, int* Errno);//有符号整数
    ssize_t WriteFd(int fd, int* Errno);

private:
    char* BeginPtr_();//命名约定
    const char* BeginPtr_() const;
    void MakeSpace_(size_t len);

    std::vector<char> buffer_;
    std::atomic<std::size_t> readPos_;
    std::atomic<std::size_t> writePos_;

};

#endif