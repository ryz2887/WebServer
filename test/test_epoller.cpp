// Epoller 模块单元测试（用 pipe 模拟真实 fd 行为）
// 编译：见文件末尾
#include "../code/server/epoller.h"
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <dirent.h>
#include <stdexcept>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do {                       \
    if (cond) { std::cout << "[PASS] " << msg << "\n"; ++g_pass; } \
    else      { std::cout << "[FAIL] " << msg << "\n"; ++g_fail; } \
} while (0)

// 信号处理函数:什么都不做,只负责把阻塞中的 epoll_wait 打断(→ EINTR)。
// 用普通函数而不是 lambda —— 信号处理函数最好是平凡可重入的。
static void NoopHandler(int) {}

int main() {
    // ---- 用例1: 构造 ----
    {
        Epoller epoller(64);
        CHECK(true, "用例1: 构造 Epoller 不崩溃");
    }

    // ---- 用例2: AddFd 参数校验 ----
    {
        Epoller epoller(64);
        CHECK(epoller.AddFd(-1, EPOLLIN) == false, "用例2: AddFd(-1) 返回 false");
    }

    // ---- 用例3: AddFd 有效 fd ----
    {
        Epoller epoller(64);
        int fds[2];
        pipe(fds);                         // fds[0]=读端, fds[1]=写端
        bool ok = epoller.AddFd(fds[0], EPOLLIN);
        CHECK(ok == true, "用例3: AddFd 有效 fd 返回 true");
        close(fds[0]);
        close(fds[1]);
    }

    // ---- 用例4: Wait 超时 ----
    {
        Epoller epoller(64);
        int fds[2];
        pipe(fds);
        epoller.AddFd(fds[0], EPOLLIN);
        // 没写数据，读端不可读，Wait(0) 立即返回 0
        int n = epoller.Wait(0);
        CHECK(n == 0, "用例4: 无事件时 Wait(0) 返回 0");
        close(fds[0]);
        close(fds[1]);
    }

    // ---- 用例5: Wait 检测到可读事件 ----
    {
        Epoller epoller(64);
        int fds[2];
        pipe(fds);
        epoller.AddFd(fds[0], EPOLLIN);
        write(fds[1], "x", 1);             // 往写端写一个字节
        int n = epoller.Wait(0);
        CHECK(n == 1, "用例5-1: 写数据后 Wait(0) 返回 1");
        CHECK(epoller.GetEventFd(0) == fds[0], "用例5-2: GetEventFd(0) 是读端");
        CHECK(epoller.GetEvents(0) & EPOLLIN, "用例5-3: GetEvents(0) 包含 EPOLLIN");
        close(fds[0]);
        close(fds[1]);
    }

    // ---- 用例6: ModFd ----
    {
        Epoller epoller(64);
        int fds[2];
        pipe(fds);
        epoller.AddFd(fds[0], EPOLLIN);
        // 修改为同时监听读和边缘触发
        bool ok = epoller.ModFd(fds[0], EPOLLIN | EPOLLET);
        CHECK(ok == true, "用例6: ModFd 返回 true");
        close(fds[0]);
        close(fds[1]);
    }

    // ---- 用例7: ModFd 无效 fd ----
    {
        Epoller epoller(64);
        CHECK(epoller.ModFd(-1, EPOLLIN) == false, "用例7: ModFd(-1) 返回 false");
    }

    // ---- 用例8: DelFd ----
    {
        Epoller epoller(64);
        int fds[2];
        pipe(fds);
        epoller.AddFd(fds[0], EPOLLIN);
        epoller.DelFd(fds[0]);              // 移除
        write(fds[1], "x", 1);              // 写入数据
        int n = epoller.Wait(0);             // 但已经不再监听了
        CHECK(n == 0, "用例8: DelFd 后 Wait 不再返回该 fd");
        close(fds[0]);
        close(fds[1]);
    }

    // ---- 用例9: 析构 close epollFd ----
    {
        // 构造后析构，不泄漏 fd
        Epoller* p = new Epoller(64);
        delete p;
        CHECK(true, "用例9: 析构不崩溃");
    }

    // ---- 用例10: Wait 超时 -1（默认参数） ----
    {
        Epoller epoller(64);
        int fds[2];
        pipe(fds);
        epoller.AddFd(fds[0], EPOLLIN);
        write(fds[1], "hello", 5);
        // Wait 不传参 = 用默认 -1 = 永久等，但因为有数据所以立即返回
        int n = epoller.Wait();
        CHECK(n == 1, "用例10: 有数据时 Wait() 默认参数也能返回");
        close(fds[0]);
        close(fds[1]);
    }

    // ================= 以下为新增:saveErrno 出参语义 =================

    // ---- 用例11: AddFd 重复注册同一 fd → EEXIST ----
    {
        Epoller epoller(64);
        int fds[2];
        pipe(fds);
        int err = 0x5A5A;      // 非零哨兵:成功路径绝不能碰它,碰了就说明契约坏了
        CHECK(epoller.AddFd(fds[0], EPOLLIN, &err) == true,  "用例11-1: 首次 AddFd 成功");
        CHECK(err == 0x5A5A, "用例11-2: 成功时不写出参(sentinel 保持不变)");
        err = 0;
        CHECK(epoller.AddFd(fds[0], EPOLLIN, &err) == false, "用例11-3: 重复 AddFd 返回 false");
        CHECK(err == EEXIST, std::string("用例11-4: saveErrno == EEXIST (实际: ") + strerror(err) + ")");
        close(fds[0]);
        close(fds[1]);
    }

    // ---- 用例12: AddFd 一个不支持 epoll 的 fd → EPERM ----
    // 这是真实走通"epoll_ctl 失败 → saveErrno"整条路径的用例:
    // 普通文件不支持 epoll,内核返回 EPERM。
    // 生产环境里同一条路径由 ENOSPC(超 max_user_watches)/ ENOMEM 触发。
    {
        Epoller epoller(64);
        int regfd = open("/etc/hosts", O_RDONLY);   // 普通文件,不是 socket/pipe
        CHECK(regfd >= 0, "用例12-1: 能打开一个普通文件");
        int err = 0;
        bool ok = epoller.AddFd(regfd, EPOLLIN, &err);
        CHECK(ok == false, "用例12-2: AddFd 普通文件返回 false");
        CHECK(err == EPERM, std::string("用例12-3: saveErrno == EPERM (实际: ") + strerror(err) + ")");
        close(regfd);
    }

    // ---- 用例13: ModFd 未注册的 fd → ENOENT ----
    {
        Epoller epoller(64);
        int fds[2];
        pipe(fds);
        int err = 0;
        CHECK(epoller.ModFd(fds[0], EPOLLIN, &err) == false, "用例13-1: ModFd 未注册 fd 返回 false");
        CHECK(err == ENOENT, std::string("用例13-2: saveErrno == ENOENT (实际: ") + strerror(err) + ")");
        close(fds[0]);
        close(fds[1]);
    }

    // ---- 用例14: DelFd 未注册的 fd → ENOENT(幂等:调用方可以当成功)----
    {
        Epoller epoller(64);
        int fds[2];
        pipe(fds);
        int err = 0;
        epoller.AddFd(fds[0], EPOLLIN);
        CHECK(epoller.DelFd(fds[0], &err) == true, "用例14-1: DelFd 已注册的 fd 成功");
        err = 0;
        CHECK(epoller.DelFd(fds[0], &err) == false, "用例14-2: 重复 DelFd 返回 false");
        CHECK(err == ENOENT, std::string("用例14-3: saveErrno == ENOENT (实际: ") + strerror(err) + ")");
        close(fds[0]);
        close(fds[1]);
    }

    // ---- 用例15: fd < 0 时出参也要有意义(不能留脏值)----
    {
        Epoller epoller(64);
        int err = 0;
        CHECK(epoller.AddFd(-1, EPOLLIN, &err) == false, "用例15-1: AddFd(-1) 返回 false");
        CHECK(err == EBADF, std::string("用例15-2: saveErrno == EBADF (实际: ") + strerror(err) + ")");
        err = 0;
        CHECK(epoller.ModFd(-1, EPOLLIN, &err) == false, "用例15-3: ModFd(-1) 返回 false");
        CHECK(err == EBADF, "用例15-4: ModFd(-1) 的 saveErrno == EBADF");
        err = 0;
        CHECK(epoller.DelFd(-1, &err) == false, "用例15-5: DelFd(-1) 返回 false");
        CHECK(err == EBADF, "用例15-6: DelFd(-1) 的 saveErrno == EBADF");
    }

    // ---- 用例16: 向后兼容 —— 不传 saveErrno 必须照样能用 ----
    // 这条守着"默认参数 = nullptr"的承诺:既有 5 个调用点一行都不用改。
    {
        Epoller epoller(64);
        int fds[2];
        pipe(fds);
        CHECK(epoller.AddFd(fds[0], EPOLLIN) == true, "用例16-1: AddFd 不传第3参仍可用");
        CHECK(epoller.ModFd(fds[0], EPOLLIN | EPOLLET) == true, "用例16-2: ModFd 不传第3参仍可用");
        CHECK(epoller.DelFd(fds[0]) == true, "用例16-3: DelFd 不传第2参仍可用");
        close(fds[0]);
        close(fds[1]);
    }

    // ---- 用例17: 显式传 nullptr 也不能崩 ----
    {
        Epoller epoller(64);
        CHECK(epoller.AddFd(-1, EPOLLIN, nullptr) == false, "用例17: 显式传 nullptr 安全");
    }

    // ================= 以下为新增:#2 构造抛异常 / #3 EINTR =================

    // ---- 用例18: 构造函数失败必须抛,不能静默构造出废对象 ----
    {
        bool threw = false;
        try {
            Epoller bad(0);           // maxEvent = 0
            (void)bad;                // 不该走到这里
        } catch(const std::invalid_argument&) {
            threw = true;
        } catch(...) {}
        CHECK(threw, "用例18: Epoller(0) 抛 std::invalid_argument");
    }

    // ---- 用例19: 抛异常时不能泄漏 epoll fd ----
    // 这条专门验证构造函数抛异常时的那个陷阱:对象没构造完整 → 析构**不会被调用**
    // → 已经拿到的 epollFd_ 必须自己 close,否则每失败一次泄漏一个 fd。
    {
        auto countEpollFds = []() {
            int c = 0;
            DIR* d = opendir("/proc/self/fd");
            if(!d) return c;
            struct dirent* e;
            while((e = readdir(d))) {
                if(e->d_name[0] == '.') continue;
                /* lnk 要比 tgt 大:前缀 "/proc/self/fd/" 14 字节 + d_name 最长 255。
                   开成一样的 256 会触发 -Wformat-truncation(确实可能被截断)。 */
                char lnk[320], tgt[256];
                snprintf(lnk, sizeof lnk, "/proc/self/fd/%s", e->d_name);
                ssize_t r = readlink(lnk, tgt, sizeof tgt - 1);
                if(r > 0) {
                    tgt[r] = 0;
                    if(strstr(tgt, "eventpoll")) c++;
                }
            }
            closedir(d);
            return c;
        };
        int before = countEpollFds();
        for(int i = 0; i < 10; i++) {
            try { Epoller bad(0); } catch(...) {}
        }
        int after = countEpollFds();
        CHECK(after == before,
              std::string("用例19: 10 次构造抛异常后 epoll fd 数不变 (")
              + std::to_string(before) + " → " + std::to_string(after) + ")");
    }

    // ---- 用例20: Wait 必须对 EINTR 重试 ----
    // 这条在**旧代码上会失败**:旧实现直接 `return epoll_wait(...)`,第一个信号
    // 打进来(约 100ms 处)就返回 -1。新实现重试到真正的超时(500ms)。
    {
        Epoller epoller(64);
        int fds[2];
        pipe(fds);
        epoller.AddFd(fds[0], EPOLLIN);      // 只注册,不写数据 → 不会就绪

        // 每 100ms 一次 SIGALRM。不设 SA_RESTART → 系统调用被打断返回 EINTR
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = NoopHandler;
        sigaction(SIGALRM, &sa, nullptr);

        struct itimerval it;
        memset(&it, 0, sizeof it);
        it.it_interval.tv_usec = 100000;
        it.it_value.tv_usec    = 100000;
        setitimer(ITIMER_REAL, &it, nullptr);

        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        int n = epoller.Wait(500);
        clock_gettime(CLOCK_MONOTONIC, &t1);

        struct itimerval off;
        memset(&off, 0, sizeof off);
        setitimer(ITIMER_REAL, &off, nullptr);
        sa.sa_handler = SIG_DFL;
        sigaction(SIGALRM, &sa, nullptr);

        long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
        CHECK(n == 0, "用例20-1: 被信号反复打断后仍走到超时,返回 0(EINTR 被重试掉)");
        CHECK(ms >= 450, std::string("用例20-2: 真的等满了 ~500ms(实际 ")
              + std::to_string(ms) + "ms;旧代码这里约 100ms)");
        close(fds[0]);
        close(fds[1]);
    }

    std::cout << "\n==== 结果: " << g_pass << " 通过, " << g_fail << " 失败 ====\n";
    return g_fail == 0 ? 0 : 1;
}

/*
在 Ubuntu 终端里编译运行:

    cd /mnt/hgfs/Project/WebServerRecur
    g++ -std=c++14 test/test_epoller.cpp code/server/epoller.cpp -o ~/test_epoller
    ~/test_epoller
*/
