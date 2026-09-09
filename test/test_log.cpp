// Log 模块单元测试
// 编译见文件末尾注释。每个用例打印 [PASS]/[FAIL]。
#include "../code/log/log.h"
#include <iostream>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do {                       \
    if (cond) { std::cout << "[PASS] " << msg << "\n"; ++g_pass; } \
    else      { std::cout << "[FAIL] " << msg << "\n"; ++g_fail; } \
} while (0)

// 读取整个文件内容
static std::string readFile(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    return content;
}

// 生成今天日期的日志文件路径
static std::string logFilePath(const char* dir, const char* suffix) {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char buf[512];
    snprintf(buf, sizeof(buf), "%s/%04d_%02d_%02d%s",
             dir, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, suffix);
    return std::string(buf);
}

int main() {
    const char* dir = "./test_log_tmp";
    mkdir(dir, 0777);
    // 清理旧日志文件
    system(("rm -f " + std::string(dir) + "/*.log").c_str());

    // ---- 用例1: 同步模式 init + write ----
    {
        Log::Instance()->init(0, dir, ".log", 0);   // level=0, 同步
        CHECK(Log::Instance()->IsOpen(), "用例1: init后IsOpen=true");
        CHECK(Log::Instance()->GetLevel() == 0, "用例1: GetLevel=0");

        LOG_INFO("hello %s", "world");
        LOG_ERROR("error code %d", 404);

        std::string path = logFilePath(dir, ".log");
        std::string content = readFile(path.c_str());

        CHECK(!content.empty(), "用例1: 日志文件有内容");
        CHECK(content.find("[info]")  != std::string::npos, "用例1: 包含 [info]");
        CHECK(content.find("hello world") != std::string::npos, "用例1: 包含 hello world");
        CHECK(content.find("[error]") != std::string::npos, "用例1: 包含 [error]");
        CHECK(content.find("error code 404") != std::string::npos, "用例1: 包含 error code 404");
    }

    // ---- 用例2: 级别过滤 ----
    {
        Log::Instance()->SetLevel(2);                // 只写 WARN 和 ERROR
        CHECK(Log::Instance()->GetLevel() == 2, "用例2: SetLevel(2)生效");

        LOG_INFO("should not appear");               // level=1 < 2，过滤
        LOG_DEBUG("should not appear either");       // level=0 < 2，过滤
        LOG_WARN("this is a warning");               // level=2，写入
        LOG_ERROR("this is an error");               // level=3，写入

        std::string path = logFilePath(dir, ".log");
        std::string content = readFile(path.c_str());

        CHECK(content.find("should not appear") == std::string::npos,
              "用例2: INFO在level=2时被过滤");
        CHECK(content.find("this is a warning") != std::string::npos,
              "用例2: WARN写入");
        CHECK(content.find("this is an error") != std::string::npos,
              "用例2: ERROR写入");
    }

    // ---- 用例3: 异步模式 write ----
    {
        Log::Instance()->init(0, dir, ".log", 1024); // level=0, 异步
        CHECK(Log::Instance()->IsOpen(), "用例3: 异步模式init成功");

        LOG_INFO("async hello");
        LOG_DEBUG("async debug msg");
        usleep(200000);                              // 等后台线程取数据写文件

        Log::Instance()->flush();                    // 再刷一次盘（把后台线程刚写的数据落盘）

        std::string path = logFilePath(dir, ".log");
        std::string content = readFile(path.c_str());

        CHECK(content.find("async hello") != std::string::npos,
              "用例3: 异步写入 async hello");
        CHECK(content.find("async debug msg") != std::string::npos,
              "用例3: 异步写入 async debug msg");
        CHECK(content.find("[debug]") != std::string::npos,
              "用例3: 异步写入包含 [debug]");
    }

    // 清理（文件还开着也能删，Linux 允许。加上 -f 忽略权限报错）
    system(("rm -rf " + std::string(dir) + " 2>/dev/null").c_str());

    std::cout << "\n==== 结果: " << g_pass << " 通过, " << g_fail << " 失败 ====\n";
    return g_fail == 0 ? 0 : 1;
}

/*
在 Ubuntu 终端里编译运行:

    cd /mnt/hgfs/Project/WebServerRecur
    g++ -std=c++14 test/test_log.cpp code/log/log.cpp code/buffer/buffer.cpp -o ~/test_log -pthread
    ~/test_log
*/
