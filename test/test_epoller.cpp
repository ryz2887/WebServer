// Epoller 模块单元测试（用 pipe 模拟真实 fd 行为）
// 编译：见文件末尾
#include "../code/server/epoller.h"
#include <iostream>
#include <unistd.h>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do {                       \
    if (cond) { std::cout << "[PASS] " << msg << "\n"; ++g_pass; } \
    else      { std::cout << "[FAIL] " << msg << "\n"; ++g_fail; } \
} while (0)

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

    std::cout << "\n==== 结果: " << g_pass << " 通过, " << g_fail << " 失败 ====\n";
    return g_fail == 0 ? 0 : 1;
}

/*
在 Ubuntu 终端里编译运行:

    cd /mnt/hgfs/Project/WebServerRecur
    g++ -std=c++14 test/test_epoller.cpp code/server/epoller.cpp -o ~/test_epoller
    ~/test_epoller
*/
