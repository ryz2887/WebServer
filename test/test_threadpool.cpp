// ThreadPool 模块单元测试
// 编译见文件末尾注释。每个用例打印 [PASS]/[FAIL]。
#include "../code/pool/threadpool.h"
#include <iostream>
#include <atomic>
#include <chrono>
#include <stdexcept>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do {                       \
    if (cond) { std::cout << "[PASS] " << msg << "\n"; ++g_pass; } \
    else      { std::cout << "[FAIL] " << msg << "\n"; ++g_fail; } \
} while (0)

int main() {
    // 初始化日志(线程池里任务抛异常会记日志,可到 test_log_tmp/ 里查看)
    Log::Instance()->init(1, "./test_log_tmp/", ".log", 1024);

    // ---- 用例1: 基本构造 ----
    {
        ThreadPool pool(4);
        CHECK(true, "用例1: 构造4线程池未崩溃");
    }

    // ---- 用例2: 单个任务 ----
    {
        ThreadPool pool(4);
        std::atomic<int> counter{0};

        pool.AddTask([&counter]() {
            counter.fetch_add(1);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(counter.load() == 1, "用例2: 单个任务执行,counter=1");
    }

    // ---- 用例3: 多个任务-验证全部执行 ----
    {
        ThreadPool pool(4);
        std::atomic<int> counter{0};
        const int N = 100;

        for (int i = 0; i < N; i++) {
            pool.AddTask([&counter]() {
                counter.fetch_add(1);
            });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(counter.load() == N, "用例3: 100个任务全部执行完毕");
    }

    // ---- 用例4: 任务由不同线程执行 ----
    {
        ThreadPool pool(4);
        std::atomic<int> maxConcurrent{0};
        std::atomic<int> running{0};

        const int N = 20;
        for (int i = 0; i < N; i++) {
            pool.AddTask([&]() {
                int cur = running.fetch_add(1) + 1;
                // 更新同时运行的最大值
                int prev = maxConcurrent.load();
                while (cur > prev && !maxConcurrent.compare_exchange_weak(prev, cur));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                running.fetch_sub(1);
            });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        CHECK(maxConcurrent.load() > 1,
              "用例4: 多任务被多线程并行执行(最大并发>1)");
    }

    // ---- 用例5: 移动构造 ----
    {
        ThreadPool pool1(2);
        std::atomic<int> counter{0};

        pool1.AddTask([&counter]() { counter.fetch_add(1); });

        ThreadPool pool2 = std::move(pool1);
        pool2.AddTask([&counter]() { counter.fetch_add(1); });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(counter.load() >= 2, "用例5: 移动后的池正常执行任务,>=2");
    }

    // ---- 用例6: 任务抛异常,worker不崩,后续任务照常执行 ----
    {
        ThreadPool pool(2);
        std::atomic<int> ran{0};

        pool.AddTask([]() { throw std::runtime_error("boom"); });   // 会抛异常的任务
        pool.AddTask([&ran]() { ran.fetch_add(1); });               // 后续任务应照常执行

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(ran.load() == 1, "用例6: 任务抛异常后进程存活,后续任务照常执行");
    }

    // ---- 用例7: 析构等待队列中的任务全部完成(优雅退出) ----
    {
        std::atomic<int> done{0};
        {
            ThreadPool pool(2);
            for (int i = 0; i < 4; i++) {
                pool.AddTask([&done]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    done.fetch_add(1);
                });
            }
        }   // 析构:应等待 4 个任务全部跑完才返回
        CHECK(done.load() == 4, "用例7: 析构返回时剩余任务已全部完成(优雅退出)");
    }

    // ---- 用例8: 每个被接受的任务都会被执行完 ----
    {
        std::atomic<int> executed{0};
        int accepted = 0;
        {
            ThreadPool pool(4);
            for (int i = 0; i < 50; i++) {
                if (pool.AddTask([&executed]() { executed.fetch_add(1); })) {
                    accepted++;
                }
            }
        }   // 析构等待
        CHECK(executed.load() == accepted && accepted == 50,
              "用例8: 被接受的50个任务在析构前全部完成(不丢失)");
    }

    std::cout << "\n==== 结果: " << g_pass << " 通过, " << g_fail << " 失败 ====\n";
    return g_fail == 0 ? 0 : 1;
}

/*
在 Ubuntu 终端里编译运行(现在依赖 log/buffer,链接命令变长了):

    cd /mnt/hgfs/Project/WebServerRecur
    g++ -std=c++14 -g test/test_threadpool.cpp code/log/log.cpp code/buffer/buffer.cpp -o ~/test_threadpool -pthread
    ~/test_threadpool
*/
