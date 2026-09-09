// ThreadPool 模块单元测试
// 编译见文件末尾注释。每个用例打印 [PASS]/[FAIL]。
#include "../code/pool/threadpool.h"
#include <iostream>
#include <atomic>
#include <chrono>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do {                       \
    if (cond) { std::cout << "[PASS] " << msg << "\n"; ++g_pass; } \
    else      { std::cout << "[FAIL] " << msg << "\n"; ++g_fail; } \
} while (0)

int main() {
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
        std::mutex mtx;

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

    std::cout << "\n==== 结果: " << g_pass << " 通过, " << g_fail << " 失败 ====\n";
    return g_fail == 0 ? 0 : 1;
}

/*
在 Ubuntu 终端里编译运行:

    cd /mnt/hgfs/Project/WebServerRecur
    g++ -std=c++14 test/test_threadpool.cpp -o ~/test_threadpool -pthread
    ~/test_threadpool
*/
