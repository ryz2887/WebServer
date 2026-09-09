// HttpRequest 模块单元测试（仅测试解析逻辑，不涉及 MySQL）
// 编译：见文件末尾
#include "../code/http/httprequest.h"
#include "../code/buffer/buffer.h"
#include <iostream>
#include <cstring>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do {                       \
    if (cond) { std::cout << "[PASS] " << msg << "\n"; ++g_pass; } \
    else      { std::cout << "[FAIL] " << msg << "\n"; ++g_fail; } \
} while (0)

// 辅助：往 Buffer 写一段 HTTP 原始文本
static void WriteHttp(Buffer& buff, const char* text) {
    buff.Append(text, strlen(text));
}

int main() {
    // ---- 用例1: Init 后成员是空 ----
    {
        HttpRequest req;
        req.Init();
        CHECK(req.method() == "", "用例1-1: Init 后 method 为空");
        CHECK(req.path() == "", "用例1-2: Init 后 path 为空");
        CHECK(req.version() == "", "用例1-3: Init 后 version 为空");
    }

    // ---- 用例2: IsKeepAlive 版本 1.0 + keep-alive → false ----
    {
        HttpRequest req;
        Buffer buff;
        WriteHttp(buff, "GET / HTTP/1.0\r\nConnection: keep-alive\r\n\r\n");
        req.parse(buff);
        CHECK(req.IsKeepAlive() == false, "用例2: HTTP/1.0 即使 Connection:keep-alive 也返回 false");
    }

    // ---- 用例3: IsKeepAlive 版本 1.1 → true ----
    {
        HttpRequest req;
        Buffer buff;
        WriteHttp(buff, "GET / HTTP/1.1\r\nConnection: keep-alive\r\n\r\n");
        req.parse(buff);
        CHECK(req.IsKeepAlive() == true, "用例3: HTTP/1.1 + keep-alive 返回 true");
    }

    // ---- 用例4: 解析 GET 请求 — method/path/version ----
    {
        HttpRequest req;
        Buffer buff;
        WriteHttp(buff, "GET /hello.html HTTP/1.1\r\nHost: test\r\n\r\n");
        bool ok = req.parse(buff);
        CHECK(ok == true, "用例4-1: parse 返回 true");
        CHECK(req.method() == "GET", "用例4-2: method = GET");
        CHECK(req.path() == "/hello.html", "用例4-3: path = /hello.html");
        CHECK(req.version() == "1.1", "用例4-4: version = 1.1");
    }

    // ---- 用例5: ParsePath_ — "/" 自动补充 index.html ----
    {
        HttpRequest req;
        Buffer buff;
        WriteHttp(buff, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
        req.parse(buff);
        CHECK(req.path() == "/index.html", "用例5: / 自动映射为 /index.html");
    }

    // ---- 用例6: ParsePath_ — /login 补 .html ----
    {
        HttpRequest req;
        Buffer buff;
        WriteHttp(buff, "GET /login HTTP/1.1\r\nHost: x\r\n\r\n");
        req.parse(buff);
        CHECK(req.path() == "/login.html", "用例6: /login 自动补 .html");
    }

    // ---- 用例7: ParsePath_ — /register 补 .html ----
    {
        HttpRequest req;
        Buffer buff;
        WriteHttp(buff, "GET /register HTTP/1.1\r\nHost: x\r\n\r\n");
        req.parse(buff);
        CHECK(req.path() == "/register.html", "用例7: /register 补 .html");
    }

    // ---- 用例8: POST 请求解析 ----
    {
        HttpRequest req;
        Buffer buff;
        WriteHttp(buff,
            "POST /search HTTP/1.1\r\n"
            "Host: x\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\n"
            "Content-Length: 11\r\n"
            "\r\n"
            "user=abc");
        req.parse(buff);
        CHECK(req.method() == "POST", "用例8-1: method = POST");
        CHECK(req.path() == "/search", "用例8-2: POST 路径不被修改");
    }

    // ---- 用例9: URL 编码解码 — + 变空格 ----
    {
        HttpRequest req;
        Buffer buff;
        WriteHttp(buff,
            "POST /x HTTP/1.1\r\n"
            "Host: x\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\n"
            "Content-Length: 11\r\n"
            "\r\n"
            "msg=hello+world");
        req.parse(buff);
        CHECK(req.GetPost("msg") == "hello world", "用例9: + 号被解码为空格");
    }

    // ---- 用例10: URL 编码 — %XX 解码为十进制 ASCII 码 ----
    {
        HttpRequest req;
        Buffer buff;
        WriteHttp(buff,
            "POST /x HTTP/1.1\r\n"
            "Host: x\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\n"
            "Content-Length: 11\r\n"
            "\r\n"
            "val=%41");     // ConverHex('4')=4, ConverHex('1')=1 → 65 → "65" ('A' 的 ASCII)
        req.parse(buff);
        CHECK(req.GetPost("val") == "%65", "用例10: %41 被保留%并转为 65");
    }

    // ---- 用例11: GetPost 不存在的 key 返回空 ----
    {
        HttpRequest req;
        Buffer buff;
        WriteHttp(buff,
            "POST /x HTTP/1.1\r\n"
            "Host: x\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\n"
            "Content-Length: 9\r\n"
            "\r\n"
            "a=1&b=2");
        req.parse(buff);
        CHECK(req.GetPost("a") == "1", "用例11-1: a=1");
        CHECK(req.GetPost("b") == "2", "用例11-2: b=2");
        CHECK(req.GetPost("c") == "", "用例11-3: 不存在的 key 返回空");
    }

    // ---- 用例12: GetPost 重载 const char* ----
    {
        HttpRequest req;
        Buffer buff;
        WriteHttp(buff,
            "POST /x HTTP/1.1\r\n"
            "Host: x\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\n"
            "Content-Length: 9\r\n"
            "\r\n"
            "key=val");
        req.parse(buff);
        CHECK(req.GetPost(std::string("key")) == "val", "用例12-1: GetPost(const string&)");
        CHECK(req.GetPost("key") == "val", "用例12-2: GetPost(const char*)");
    }

    // ---- 用例13: 空 body POST 不崩溃 ----
    {
        HttpRequest req;
        Buffer buff;
        WriteHttp(buff,
            "POST /x HTTP/1.1\r\n"
            "Host: x\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\n"
            "Content-Length: 0\r\n"
            "\r\n"
            "");
        req.parse(buff);
        CHECK(true, "用例13: 空 body POST 不崩溃");
    }

    // ---- 用例14: 错误格式的请求行 ----
    {
        HttpRequest req;
        Buffer buff;
        WriteHttp(buff, "INVALID\r\n\r\n");
        bool ok = req.parse(buff);
        CHECK(ok == false, "用例14: 错误格式返回 false");
    }

    // ---- 用例15: path() 非 const 版本返回引用 ----
    {
        HttpRequest req;
        Buffer buff;
        WriteHttp(buff, "GET /test HTTP/1.1\r\nHost: x\r\n\r\n");
        req.parse(buff);
        req.path() = "/modified.html";
        CHECK(req.path() == "/modified.html", "用例15: 非 const path() 可修改路径");
    }

    std::cout << "\n==== 结果: " << g_pass << " 通过, " << g_fail << " 失败 ====\n";
    return g_fail == 0 ? 0 : 1;
}

/*
在 Ubuntu 终端里编译运行:

    cd /mnt/hgfs/Project/WebServerRecur
    g++ -std=c++14 test/test_httprequest.cpp code/http/httprequest.cpp \
        code/buffer/buffer.cpp code/log/log.cpp -o ~/test_httprequest -pthread
    ~/test_httprequest
*/
