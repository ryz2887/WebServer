// HttpResponse 模块单元测试
// 编译：见文件末尾。测试需要先在 /tmp 创建测试资源文件。
#include "../code/http/httpresponse.h"
#include "../code/buffer/buffer.h"
#include <iostream>
#include <fstream>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do {                       \
    if (cond) { std::cout << "[PASS] " << msg << "\n"; ++g_pass; } \
    else      { std::cout << "[FAIL] " << msg << "\n"; ++g_fail; } \
} while (0)

static const char* SRC = "/tmp/test_src/";

// 准备测试资源：/tmp/test_src/index.html 等
static void PrepareFiles() {
    system("mkdir -p /tmp/test_src");
    std::ofstream f("/tmp/test_src/index.html");
    f << "<html><body>Hello Test</body></html>";
    f.close();
}

int main() {
    PrepareFiles();

    // ---- 用例1: 构造/析构 ----
    {
        HttpResponse resp;
        CHECK(resp.Code() == -1, "用例1: 初始 code = -1");
    }

    // ---- 用例2: 正常响应 200 ----
    {
        HttpResponse resp;
        std::string path = "/index.html";
        resp.Init(SRC, path, false);
        Buffer buff;
        resp.MakeResponse(buff);
        CHECK(resp.Code() == 200, "用例2-1: 存在的文件 code=200");
        CHECK(resp.File() != nullptr, "用例2-2: mmap 成功，File() 非空");
        CHECK(resp.FileLen() > 0, "用例2-3: FileLen 等于文件大小");
    }

    // ---- 用例3: 响应文本包含状态行和头 ----
    {
        HttpResponse resp;
        std::string path = "/index.html";
        resp.Init(SRC, path, true);
        Buffer buff;
        resp.MakeResponse(buff);
        std::string content = buff.RetrieveAllToStr();
        CHECK(content.find("HTTP/1.1 200 OK") != std::string::npos, "用例3-1: 状态行 200 OK");
        CHECK(content.find("Connection: keep-alive") != std::string::npos, "用例3-2: keep-alive 头");
        CHECK(content.find("Content-type: text/html") != std::string::npos, "用例3-3: Content-type 正确");
        CHECK(content.find("Content-length:") != std::string::npos, "用例3-4: 有 Content-length");
    }

    // ---- 用例4: 不存在的文件 404 ----
    {
        HttpResponse resp;
        std::string path = "/not_exist.html";
        resp.Init(SRC, path, false);
        Buffer buff;
        resp.MakeResponse(buff);
        CHECK(resp.Code() == 404, "用例4-1: 不存在文件 code=404");
        std::string content = buff.RetrieveAllToStr();
        CHECK(content.find("404") != std::string::npos, "用例4-2: 响应里有 404");
    }

    // ---- 用例5: 无权限文件 403 ----
    {
        std::ofstream f("/tmp/test_src/secret.html");
        f << "secret";
        f.close();
        system("chmod 000 /tmp/test_src/secret.html");  // 去掉所有权限

        HttpResponse resp;
        std::string path = "/secret.html";
        resp.Init(SRC, path, false);
        Buffer buff;
        resp.MakeResponse(buff);
        CHECK(resp.Code() == 403, "用例5: 无权限文件 code=403");

        system("chmod 644 /tmp/test_src/secret.html");
    }

    // ---- 用例6: 传入 code=400（外部定好的错误码）----
    {
        HttpResponse resp;
        std::string path = "/index.html";
        resp.Init(SRC, path, false, 400);  // 外部指定错误码
        Buffer buff;
        resp.MakeResponse(buff);
        CHECK(resp.Code() == 400, "用例6-1: 外部指定的 code 被保留");
        std::string content = buff.RetrieveAllToStr();
        CHECK(content.find("400") != std::string::npos, "用例6-2: 响应里有 400");
    }

    // ---- 用例7: 非 keep-alive → Connection: close ----
    {
        HttpResponse resp;
        std::string path = "/index.html";
        resp.Init(SRC, path, false);  // 不保持连接
        Buffer buff;
        resp.MakeResponse(buff);
        std::string content = buff.RetrieveAllToStr();
        CHECK(content.find("Connection: close") != std::string::npos, "用例7: Connection: close");
    }

    // ---- 用例8: 未知后缀 → text/plain ----
    {
        std::ofstream f("/tmp/test_src/data.xyz");
        f << "data";
        f.close();

        HttpResponse resp;
        std::string path = "/data.xyz";
        resp.Init(SRC, path, false);
        Buffer buff;
        resp.MakeResponse(buff);
        std::string content = buff.RetrieveAllToStr();
        CHECK(content.find("Content-type: text/plain") != std::string::npos,
              "用例8: 未知后缀返回 text/plain");
    }

    // ---- 用例9: Init 复用同一实例（模拟多次请求）----
    {
        HttpResponse resp;
        std::string path1 = "/index.html";
        resp.Init(SRC, path1, false);
        Buffer buff1;
        resp.MakeResponse(buff1);
        resp.UnmapFile();

        std::string path2 = "/index.html";
        resp.Init(SRC, path2, false);
        Buffer buff2;
        resp.MakeResponse(buff2);
        CHECK(resp.Code() == 200, "用例9-1: 复用实例第二次响应 code=200");
        CHECK(resp.File() != nullptr, "用例9-2: 复用实例 mmap 成功");
        resp.UnmapFile();
    }

    // ---- 用例10: 文件内容与磁盘一致 ----
    {
        HttpResponse resp;
        std::string path = "/index.html";
        resp.Init(SRC, path, false);
        Buffer buff;
        resp.MakeResponse(buff);
        std::string fileContent(resp.File(), resp.FileLen());
        CHECK(fileContent.find("Hello Test") != std::string::npos,
              "用例10: mmap 内容与磁盘文件一致");
        resp.UnmapFile();
    }

    std::cout << "\n==== 结果: " << g_pass << " 通过, " << g_fail << " 失败 ====\n";
    return g_fail == 0 ? 0 : 1;
}

/*
在 Ubuntu 终端里编译运行:

    cd /mnt/hgfs/Project/WebServerRecur
    g++ -std=c++14 test/test_httpresponse.cpp code/http/httpresponse.cpp \
        code/buffer/buffer.cpp code/log/log.cpp -o ~/test_httpresponse -pthread
    ~/test_httpresponse
*/
