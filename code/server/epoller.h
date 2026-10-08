#ifndef EPOLLER_H
#define EPOLLER_H

#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <vector>
#include <errno.h>

class Epoller {
    public:
    explicit Epoller(int maxEvent = 1024);
    ~Epoller();
    /* saveErrno 可选(默认 nullptr):失败时把 errno 显式带出来。
       为什么必须由这里取走 —— errno 是带外(out-of-band)状态,不在签名里,
       调用方看见的只有 bool,会以为"这个错误可以忽略";而且 errno 是线程局部
       全局变量,返回后调用方再读,中间只要插进任何一次 libc 调用就被覆盖了。
       取在失败发生的那一刻,才是可靠的。 */
    bool AddFd(int fd, uint32_t events, int* saveErrno = nullptr);
    bool ModFd(int fd, uint32_t events, int* saveErrno = nullptr);
    bool DelFd(int fd, int* saveErrno = nullptr);
    int Wait(int timeoutMs = -1);
    int GetEventFd(size_t i) const;
    uint32_t GetEvents(size_t i) const;
    private:
    int epollFd_;
    std::vector<struct epoll_event> events_;
};

#endif