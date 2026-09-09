// HttpConn 模块单元测试（用 socketpair 模拟客户端-服务器连接）
// 编译：见文件末尾
#include "../code/http/httpconn.h"
#include <iostream>
#include <sys/socket.h>
#include <cstring>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do {                       \
    if (cond) { std::cout << "[PASS] " << msg << "\n"; ++g_pass; } \
    else      { std::cout << "[FAIL] " << msg << "\n"; ++g_fail; } \
} while (0)

int main() {
    // 准备测试资源目录
    system("mkdir -p /tmp/test_src");
    system("echo '<html>hello</html>' > /tmp/test_src/index.html");
    HttpConn::srcDir = "/tmp/test_src";
    HttpConn::isET = false;

    // ---- 用例1: 初始状态 ----
    {
        HttpConn conn;
        CHECK(conn.GetFd() == -1, "用例1: 构造后 fd = -1");
    }

    // ---- 用例2: init 后 userCount +1 ----
    {
        int fds[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
        sockaddr_in addr = { 0 };
        addr.sin_family = AF_INET;

        int before = HttpConn::userCount;
        HttpConn conn;
        conn.init(fds[0], addr);
        CHECK(HttpConn::userCount == before + 1, "用例2-1: init 后 userCount +1");
        CHECK(conn.GetFd() == fds[0], "用例2-2: GetFd 返回 fd");
        conn.Close();
        CHECK(HttpConn::userCount == before, "用例2-3: Close 后 userCount -1");
        close(fds[1]);
    }

    // ---- 用例3: read 读到客户端发来的数据 ----
    {
        int fds[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
        sockaddr_in addr = { 0 };
        HttpConn conn;
        conn.init(fds[0], addr);

        const char* msg = "hello from client";
        send(fds[1], msg, strlen(msg), 0);   // 模拟客户端发送

        int err = 0;
        ssize_t len = conn.read(&err);
        CHECK(len == (ssize_t)strlen(msg), "用例3-1: read 返回正确长度");
        CHECK(conn.ToWriteBytes() == 0, "用例3-2: 还没生成响应，待发字节为 0");
        conn.Close();
        close(fds[1]);
    }

    // ---- 用例4: process 解析 GET 请求并生成响应 ----
    {
        int fds[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
        sockaddr_in addr = { 0 };
        HttpConn conn;
        conn.init(fds[0], addr);

        // 模拟完整 HTTP GET 请求
        const char* req = "GET /index.html HTTP/1.1\r\nHost: test\r\nConnection: keep-alive\r\n\r\n";
        send(fds[1], req, strlen(req), 0);

        int err = 0;
        conn.read(&err);
        bool ok = conn.process();
        CHECK(ok == true, "用例4-1: process 返回 true");
        CHECK(conn.ToWriteBytes() > 0, "用例4-2: 有待发送的响应字节");
        CHECK(conn.IsKeepAlive() == true, "用例4-3: keep-alive 连接");

        // 收响应验证内容
        char recvBuf[4096] = { 0 };
        conn.write(&err);                    // ← process 只是准备，write 才发送
        recv(fds[1], recvBuf, sizeof(recvBuf), 0);
        CHECK(strstr(recvBuf, "HTTP/1.1 200 OK") != nullptr, "用例4-4: 响应含状态行 200");
        CHECK(strstr(recvBuf, "Content-length:") != nullptr, "用例4-5: 响应含 Content-length");
        conn.Close();
        close(fds[1]);
    }

    // ---- 用例5: write 发送响应 ----
    {
        int fds[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
        sockaddr_in addr = { 0 };
        HttpConn conn;
        conn.init(fds[0], addr);

        const char* req = "GET /index.html HTTP/1.0\r\nHost: test\r\n\r\n";
        send(fds[1], req, strlen(req), 0);

        int err = 0;
        conn.read(&err);
        conn.process();

        // 客户端收响应
        char recvBuf[8192] = { 0 };
        conn.write(&err);                    // ← 先发送再接收
        ssize_t got = recv(fds[1], recvBuf, sizeof(recvBuf), 0);
        CHECK(got > 0, "用例5-1: 客户端收到响应");
        CHECK(strstr(recvBuf, "HTTP/1.1 200 OK") != nullptr, "用例5-2: 状态行正确");
        CHECK(strstr(recvBuf, "<html>hello</html>") != nullptr, "用例5-3: 文件内容正确");
        conn.Close();
        close(fds[1]);
    }

    // ---- 用例6: 404 响应 ----
    {
        int fds[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
        sockaddr_in addr = { 0 };
        HttpConn conn;
        conn.init(fds[0], addr);

        const char* req = "GET /notexist.html HTTP/1.0\r\nHost: test\r\n\r\n";
        send(fds[1], req, strlen(req), 0);

        int err = 0;
        conn.read(&err);
        conn.process();

        char recvBuf[4096] = { 0 };
        conn.write(&err);                    // ← 先发送再接收
        recv(fds[1], recvBuf, sizeof(recvBuf), 0);
        CHECK(strstr(recvBuf, "404") != nullptr, "用例6: 不存在的文件返回 404");
        conn.Close();
        close(fds[1]);
    }

    // ---- 用例7: 空数据 process 返回 false ----
    {
        int fds[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
        sockaddr_in addr = { 0 };
        HttpConn conn;
        conn.init(fds[0], addr);

        CHECK(conn.process() == false, "用例7: 没有数据时 process 返回 false");
        conn.Close();
        close(fds[1]);
    }

    std::cout << "\n==== 结果: " << g_pass << " 通过, " << g_fail << " 失败 ====\n";
    return g_fail == 0 ? 0 : 1;
}

/*
在 Ubuntu 终端里编译运行:

    cd /mnt/hgfs/Project/WebServerRecur
    g++ -std=c++14 test/test_httpconn.cpp code/http/httpconn.cpp \
        code/http/httprequest.cpp code/http/httpresponse.cpp \
        code/buffer/buffer.cpp code/log/log.cpp code/pool/sqlconnpool.cpp \
        -o ~/test_httpconn -pthread -lmysqlclient
    ~/test_httpconn
*/
