#include "buffer.h"
Buffer::Buffer(int initBuffSize):buffer_(initBuffSize),readPos_(0),writePos_(0) {}
size_t Buffer::WritableBytes() const{
    return buffer_.size() - writePos_;
}
size_t Buffer::ReadableBytes() const{
    return writePos_ - readPos_;
}
size_t Buffer::PrependableBytes() const{
    return readPos_;
}
const char* Buffer::Peek() const{
    return BeginPtr_() + readPos_;
}
void Buffer::HasWritten(size_t len){
    writePos_ += len;
}
void Buffer::Retrieve(size_t len){
    assert(len <= ReadableBytes());
    readPos_ += len;
}
const char* Buffer::BeginWriteConst() const {
    return BeginPtr_() + writePos_;
}
char* Buffer::BeginWrite(){
    return BeginPtr_() + writePos_;
}
char* Buffer::BeginPtr_(){
    return &*buffer_.begin();
}
const char* Buffer::BeginPtr_() const{
    return &*buffer_.begin();
}
void Buffer::MakeSpace_(size_t len){
    if(buffer_.size() - writePos_ + readPos_ >= len){
        size_t readable = ReadableBytes();                                   // 1. 先记住有多少数据
        std::copy(BeginPtr_() + readPos_, BeginPtr_() + writePos_, BeginPtr_()); // 2. 把有效数据搬到最前面
        readPos_ = 0;                                                        // 3. 重设两个指针
        writePos_ = readable;
    }else{
        buffer_.resize(len + writePos_);
    }
}
void Buffer::EnsureWriteable(size_t len){
    if(WritableBytes() < len){
        MakeSpace_(len);
    }
}
void Buffer::Append(const char* str, size_t len){
    EnsureWriteable(len);
    std::copy(str, str + len, BeginWrite());
    HasWritten(len);
}
void Buffer::Append(const std::string& str){
    Append(str.data(), str.length());
}
void Buffer::Append(const void* data, size_t len){
    Append(static_cast<const char*>(data), len);
}
void Buffer::Append(const Buffer& buff){
    Append(buff.Peek(), buff.ReadableBytes());
}
void Buffer::RetrieveUntil(const char* end){
    Retrieve(end - Peek());
}
void Buffer::RetrieveAll(){
    writePos_ = 0;
    readPos_ = 0;
} 
std::string Buffer::RetrieveAllToStr(){
    std::string str(Peek(),ReadableBytes());
    RetrieveAll();
    return str;
}
ssize_t Buffer::WriteFd(int fd, int* saveErrno){
    ssize_t len = write(fd, Peek(), ReadableBytes()); // 从可读起点,发 ReadableBytes 个字节
    if(len < 0){ *saveErrno = errno; return len; }    // 出错:记下错误码
    readPos_ += len;                                   // 发出去多少,读位置就前进多少
    return len;
}
ssize_t Buffer::ReadFd(int fd, int* saveErrno){
    char buff[65535];              // ② 栈上临时后备,64KB
    struct iovec iov[2];           // iovec 描述"一块内存":起始地址 + 长度
    const size_t writable = WritableBytes();

    iov[0].iov_base = BeginPtr_() + writePos_;  // ① 第一块 = 缓冲区可写区
    iov[0].iov_len  = writable;
    iov[1].iov_base = buff;                      // ② 第二块 = 临时数组
    iov[1].iov_len  = sizeof(buff);

    const ssize_t len = readv(fd, iov, 2);       // 一次读入,自动先填①再填②
    if(len < 0){
        *saveErrno = errno;
    }
    else if(static_cast<size_t>(len) <= writable){ // 没溢出:①就装下了
        writePos_ += len;
    }
    else {                                          // 溢出了:①满,剩下的在②
        writePos_ = buffer_.size();                 // ①标记为写满
        Append(buff, len - writable);               // 把②里多出来的追加进来(会自动扩容)
    }
    return len;
}
