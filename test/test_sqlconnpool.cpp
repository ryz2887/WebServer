// SqlConnPool 模块单元测试（连接池结构验证，不连接真实 MySQL）
// 编译见文件末尾注释。每个用例打印 [PASS]/[FAIL]。
#include "../code/pool/sqlconnpool.h"
#include "../code/pool/sqlconnRAII.h"
#include <iostream>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do {                       \
    if (cond) { std::cout << "[PASS] " << msg << "\n"; ++g_pass; } \
    else      { std::cout << "[FAIL] " << msg << "\n"; ++g_fail; } \
} while (0)

int main() {
    // ---- 用例1: 单例 ----
    {
        SqlConnPool* p1 = SqlConnPool::Instance();
        SqlConnPool* p2 = SqlConnPool::Instance();
        CHECK(p1 == p2, "用例1: 两次 Instance() 返回同一实例");
    }

    // ---- 用例2: 初始状态 ----
    {
        SqlConnPool* pool = SqlConnPool::Instance();
        CHECK(pool->GetFreeConnCount() == 0, "用例2: 未 Init 时空闲连接数=0");
    }

    std::cout << "\n==== 结果: " << g_pass << " 通过, " << g_fail << " 失败 ====\n";
    return g_fail == 0 ? 0 : 1;
}

/*
在 Ubuntu 终端里编译运行:

    cd /mnt/hgfs/Project/WebServerRecur
    g++ -std=c++14 test/test_sqlconnpool.cpp code/pool/sqlconnpool.cpp \
        code/log/log.cpp code/buffer/buffer.cpp -o ~/test_sqlconnpool \
        -pthread -lmysqlclient
    ~/test_sqlconnpool

注意：需要先安装 MySQL：
    sudo apt install -y mysql-server libmysqlclient-dev

如果 MySQL 还没装，也可以先不编译这个测试，等模块 8 整个项目联调时再测。
*/
