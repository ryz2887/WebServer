// Buffer 模块单元测试
// 编译见文件末尾注释。每个用例打印 [PASS]/[FAIL]。
#include "../code/buffer/buffer.h"
#include <iostream>
#include <string>
#include <unistd.h>   // pipe, write, close

static int g_pass = 0, g_fail = 0;

// 简易断言:cond 为真则 PASS,否则 FAIL 并打印说明
#define CHECK(cond, msg) do {                       \
    if (cond) { std::cout << "[PASS] " << msg << "\n"; ++g_pass; } \
    else      { std::cout << "[FAIL] " << msg << "\n"; ++g_fail; } \
} while (0)

int main() {
    // ---- 用例1:基本写入与读取 ----
    {
        Buffer buf;
        buf.Append(std::string("hello"));
        CHECK(buf.ReadableBytes() == 5, "用例1: append 5 字节后可读=5");
        std::string s = buf.RetrieveAllToStr();
        CHECK(s == "hello", "用例1: 取出的内容为 hello");
        CHECK(buf.ReadableBytes() == 0, "用例1: 取完后可读=0");
    }

    // ---- 用例2:自动扩容(触发 MakeSpace_ 的 resize 分支)----
    {
        Buffer buf(8);                       // 故意只开 8 字节
        std::string big(20, 'x');            // 20 个 'x',超过初始容量
        buf.Append(big);
        CHECK(buf.ReadableBytes() == 20, "用例2: 写入 20 字节后可读=20(已自动扩容)");
        CHECK(buf.RetrieveAllToStr() == big, "用例2: 扩容后内容完整无损");
    }

    // ---- 用例3:MakeSpace_ 的"搬家"分支(复用已读空间)----
    {
        Buffer buf(10);
        buf.Append(std::string("ABCDEFGH")); // 写 8,writePos=8
        buf.Retrieve(6);                      // 读走 6,readPos=6,剩 "GH"
        // 此刻:可写=2,已读区=6。再写 5 字节:2<5 但 2+6>=5 → 走搬家分支
        buf.Append(std::string("12345"));
        CHECK(buf.ReadableBytes() == 7, "用例3: 搬家后可读=7 (GH + 12345)");
        CHECK(buf.RetrieveAllToStr() == "GH12345", "用例3: 搬家后数据顺序正确");
    }

    // ---- 用例4:ReadFd / WriteFd 真正对 fd 收发 ----
    {
        int fds[2];
        if (pipe(fds) != 0) {                 // 建一个管道:fds[0]读端,fds[1]写端
            std::cout << "[FAIL] 用例4: 无法创建管道\n"; ++g_fail;
        } else {
            const char* msg = "network-data";
            write(fds[1], msg, 12);           // 往管道写端塞数据(模拟客户端发来)
            close(fds[1]);                    // 关写端,表示数据发完

            Buffer buf;
            int err = 0;
            ssize_t n = buf.ReadFd(fds[0], &err);   // 从管道读端收进 buffer
            CHECK(n == 12, "用例4: ReadFd 读到 12 字节");
            CHECK(buf.RetrieveAllToStr() == "network-data", "用例4: 收到的内容正确");
            close(fds[0]);
        }
    }

    std::cout << "\n==== 结果: " << g_pass << " 通过, " << g_fail << " 失败 ====\n";
    return g_fail == 0 ? 0 : 1;
}

/*
在 Ubuntu 终端里编译运行:

    cd /mnt/hgfs/Project/WebServerRecur
    g++ -std=c++14 test/test_buffer.cpp code/buffer/buffer.cpp -o ~/test_buffer -pthread
    ~/test_buffer
*/
