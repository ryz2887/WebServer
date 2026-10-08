#include "epoller.h"

#include <system_error>
#include <stdexcept>
#include <time.h>       // clock_gettime / CLOCK_MONOTONIC(Wait 计算剩余超时用)

/* 构造函数失败为什么必须抛 —— 三条路掐掉两条:
   ① assert:在 -DNDEBUG(Release 常见)下被**整个编译掉**。那时 epoll_create 失败
      (返回 -1)被静默忽略,对象带着 epollFd_ = -1 "构造成功";之后每次 epoll_ctl /
      epoll_wait 全部失败。症状是"服务器起来了但一个连接都接不了",且零日志。
   ② 日志:在**这里**打日志是静默 no-op。Epoller 是在 WebServer 的初始化列表里构造的,
      而 Log::Instance()->init() 在那之后才调用 —— 此时 isOpen_ 还是 false,
      LOG_BASE 宏(log.h:61)整条跳过,一行都不会输出。
   → 构造函数没法"边构造边返回错误",唯一能让上层看见的办法就是抛。 */
Epoller::Epoller(int maxEvent):epollFd_(epoll_create(512)), events_(maxEvent) {
    if(epollFd_ < 0) {
        throw std::system_error(errno, std::generic_category(), "epoll_create(512) failed");
    }
    if(events_.empty()) {
        /* 关键:构造函数抛异常时**析构函数不会被调用**(对象没构造完整),
           所以上面已经拿到的 epollFd_ 必须在这里自己释放,否则就是 fd 泄漏。 */
        close(epollFd_);
        throw std::invalid_argument("Epoller: maxEvent must be > 0");
    }
}

Epoller::~Epoller() {
    close(epollFd_);
}

bool Epoller::AddFd(int fd, uint32_t events, int* saveErrno) {
    if(fd < 0) {
        if(saveErrno) *saveErrno = EBADF;   // 参数就不合法,谈不上系统调用
        return false;
    }
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    if(0 == epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev)) return true;
    if(saveErrno) *saveErrno = errno;       // 失败当场取走,别留给调用方去猜
    return false;
}

bool Epoller::ModFd(int fd, uint32_t events, int* saveErrno) {
    if(fd < 0) {
        if(saveErrno) *saveErrno = EBADF;
        return false;
    }
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    if(0 == epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev)) return true;
    if(saveErrno) *saveErrno = errno;       // 最常见的失败是 ENOENT(fd 没注册过)
    return false;
}

bool Epoller::DelFd(int fd, int* saveErrno) {
    if(fd < 0) {
        if(saveErrno) *saveErrno = EBADF;
        return false;
    }
    epoll_event ev = {0};
    if(0 == epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, &ev)) return true;
    if(saveErrno) *saveErrno = errno;       // ENOENT = 本来就没注册,调用方可以当幂等成功
    return false;
}

int Epoller::Wait(int timeoutMs) {
    /* 这里有两个容易踩的点。

   【一】必须重试 EINTR。epoll_wait 被信号打断会返回 -1/errno=EINTR —— 那不是失败。
         不重试的话主循环会拿到 -1 → for 循环不进 → 回到 while 再 Wait,看起来
         "什么都没发生",但白跑一轮;如果 errno 是永久性错误还会一直空转。

   【二】重试**不能**用原来的 timeoutMs!epoll_wait 被打断后**不会从剩余时间续上**,
         每次调用都是一个全新的超时。所以"打断就用原值重试"在
         "信号比超时来得还频繁"时会死循环:
             信号每 100ms 一次 + 超时 500ms → 每 100ms 就把 500ms 的时钟重置一次
             → 永远等不到 500ms 到期 → 无限循环。
         (这个 bug 就是本文件的单元测试 `用例20` 抓出来的,写的时候没想到。)
         正确做法:先记一个**绝对截止时刻**,每次重试只传"还剩多少毫秒"。
    */
    struct timespec deadline = {0, 0};
    const bool hasDeadline = (timeoutMs >= 0);   // < 0 = 永久等待,没有截止时刻
    if(hasDeadline) {
        clock_gettime(CLOCK_MONOTONIC, &deadline);
        deadline.tv_sec  += timeoutMs / 1000;
        deadline.tv_nsec += static_cast<long>(timeoutMs % 1000) * 1000000L;
        if(deadline.tv_nsec >= 1000000000L) {    // 纳秒进位
            deadline.tv_sec  += 1;
            deadline.tv_nsec -= 1000000000L;
        }
    }

    for(;;) {
        /* 用 events_.data() 而不是 &events_[0]:后者对空 vector 是 UB
           (C++11 起 operator[] 越界明确是 UB)。当前靠构造函数的检查兜着,
           但 data() 无论空不空都合法,这一行就自洽了,不用依赖别处。 */
        int n = epoll_wait(epollFd_, events_.data(),
                           static_cast<int>(events_.size()), timeoutMs);
        if(n >= 0) return n;                     // 有事件 / 正常超时
        if(errno != EINTR) {
            /* 真错误(EBADF/EINVAL/EFAULT)。
               **这里故意不记日志** —— Epoller 是比 Log 更底层的**机制**封装,
               Log 是**策略**。让机制反向依赖策略是分层倒置:会把 Log → Buffer
               整条依赖链拖进来(想单独用 Epoller 就得连日志系统一起链),
               而且"错误怎么报、要不要停"本来是应用层的决定。
               所以只把结果如实返回,由调用方(WebServer::Start,那里有 Log)
               去报告。 */
            return n;
        }
        if(!hasDeadline) continue;               // -1 永久等待:没有截止时刻,原值重试

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long long remainNs =
            static_cast<long long>(deadline.tv_sec - now.tv_sec) * 1000000000LL
            + (deadline.tv_nsec - now.tv_nsec);
        if(remainNs <= 0) return 0;              // 已过截止时刻 → 等价于"超时,无事件"
        timeoutMs = static_cast<int>((remainNs + 999999) / 1000000);  // 向上取整,至少 1ms
    }
}

int Epoller::GetEventFd(size_t i) const {
    assert(i < events_.size());     // i 是 size_t,原来那句 "&& i >= 0" 恒真,编译器要警告
    return events_[i].data.fd;
}

uint32_t Epoller::GetEvents(size_t i) const {
    assert(i < events_.size());
    return events_[i].events;
}