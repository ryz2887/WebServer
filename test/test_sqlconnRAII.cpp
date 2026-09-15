// SqlConnRAII 集成测试:验证"借了必还、只还一次"(需要本地 MySQL 已启动)
// 编译运行见文件末尾注释。每个用例打印 [PASS]/[FAIL]。
//
// 这是 2026-09-15 修复 UserVerify 连接双重归还后补的回归测试:
// 修复前,每次"借了用完还"会让池子里的连接数虚增(+1/次,因为临时对象提前还了一次、
// 函数末尾又手动还了一次);修复后应始终守恒。
#include "../code/pool/sqlconnRAII.h"
#include "../code/log/log.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do {                                       \
    if (cond) { std::cout << "[PASS] " << msg << "\n"; ++g_pass; }  \
    else      { std::cout << "[FAIL] " << msg << "\n"; ++g_fail; }  \
} while (0)

int main() {
    Log::Instance()->init(0, "./test_log_tmp/", ".log", 1024);

    SqlConnPool* pool = SqlConnPool::Instance();
    pool->Init("localhost", 3306, "root", "root", "webserver", 4);   // 4条连接,方便观察

    CHECK(pool->GetFreeConnCount() == 4, "用例1: Init后空闲连接=4");

    // ---- 用例2/3: 借用期间连接确实被拿在手里,离开作用域才归还 ----
    {
        MYSQL* sql;
        SqlConnRAII raii(&sql, pool);      // 命名对象:活到作用域结束
        CHECK(sql != nullptr && pool->GetFreeConnCount() == 3,
              "用例2: 借用期间空闲连接=3(连接在手里,没被提前归还)");
    }
    CHECK(pool->GetFreeConnCount() == 4,
          "用例3: 离开作用域后空闲连接回到4(析构自动归还)");

    // ---- 用例4: 连续借还5次,连接数不虚增(修复前会变成 4+5=9) ----
    for (int i = 0; i < 5; i++) {
        MYSQL* sql;
        SqlConnRAII raii(&sql, pool);
        if (sql) { mysql_ping(sql); }
    }
    CHECK(pool->GetFreeConnCount() == 4,
          "用例4: 5次借还后空闲连接仍是4(没有重复归还)");

    // ---- 用例5/6: 并发借还,不崩不漏 ----
    {
        std::atomic<int> okCount{0};
        auto work = [&]() {
            for (int i = 0; i < 5; i++) {
                MYSQL* sql;
                SqlConnRAII raii(&sql, pool);
                if (sql && mysql_ping(sql) == 0) { okCount++; }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        };
        std::thread t1(work), t2(work);
        t1.join(); t2.join();

        CHECK(okCount.load() == 10, "用例5: 并发10次借用全部成功");
        CHECK(pool->GetFreeConnCount() == 4, "用例6: 并发借还后空闲连接仍是4");
    }

    std::cout << "\n==== 结果: " << g_pass << " 通过, " << g_fail << " 失败 ====\n";
    return g_fail == 0 ? 0 : 1;
}

/*
在 Ubuntu 终端里编译运行(需要 MySQL 已启动,账号/库与 main.cpp 一致):

    cd /mnt/hgfs/Project/WebServerRecur
    g++ -std=c++14 -g test/test_sqlconnRAII.cpp code/pool/*.cpp code/log/*.cpp code/buffer/*.cpp -o ~/test_sqlconnRAII -pthread -lmysqlclient
    ~/test_sqlconnRAII

注意:SqlConnPool 是单例,Init 只能调一次。
*/
