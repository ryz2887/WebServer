// HeapTimer 模块单元测试
// 编译：见文件末尾
#include "../code/timer/heaptimer.h"
#include <iostream>
#include <thread>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do {                       \
    if (cond) { std::cout << "[PASS] " << msg << "\n"; ++g_pass; } \
    else      { std::cout << "[FAIL] " << msg << "\n"; ++g_fail; } \
} while (0)

int main() {
    // ---- 用例1: add + tick（超时=0，立即触发） ----
    {
        HeapTimer timer;
        int called = 0;
        timer.add(1, 0, [&called]{ called++; });
        timer.tick();
        CHECK(called == 1, "用例1: 超时=0，tick 后回调被执行");
    }

    // ---- 用例2: 超时很长，不会触发 ----
    {
        HeapTimer timer;
        int called = 0;
        timer.add(2, 600000, [&called]{ called++; });  // 600 秒
        timer.tick();
        CHECK(called == 0, "用例2: 超时未到，tick 不触发回调");
    }

    // ---- 用例3: 多个定时器按到期顺序触发 ----
    {
        HeapTimer timer;
        int order = 0, a = 0, b = 0, c = 0;
        // 超时设为 0，放入顺序 c, a, b
        timer.add(10, 0, [&]{ a = ++order; });
        timer.add(20, 0, [&]{ b = ++order; });
        timer.add(30, 0, [&]{ c = ++order; });

        timer.tick();
        // 三个都到期，但堆应该按 id 无关，只按 expires
        // expires 都是同时间，顺序取决于堆实现
        CHECK(a > 0 && b > 0 && c > 0, "用例3: 三个超时回调都被调用");
    }

    // ---- 用例4: add 更新已有 id ----
    {
        HeapTimer timer;
        int oldCb = 0, newCb = 0;
        timer.add(5, 0, [&]{ oldCb++; });
        timer.add(5, 100, [&]{ newCb++; });  // 同 id，更新为 100ms

        timer.tick();
        CHECK(oldCb == 0 && newCb == 0, "用例4-1: 更新后原回调未触发");
        CHECK(timer.GetNextTick() >= 0, "用例4-2: 更新后堆不为空");
    }

    // ---- 用例5: adjust 调整超时 ----
    {
        HeapTimer timer;
        int called = 0;
        timer.add(7, 5000, [&]{ called++; });     // 5 秒
        timer.adjust(7, 0);                        // 改为 0 = 立即超时
        timer.tick();
        CHECK(called == 1, "用例5: adjust 后提前触发回调");
    }

    // ---- 用例6: doWork 主动触发 ----
    {
        HeapTimer timer;
        int called = 0;
        timer.add(9, 600000, [&]{ called++; });
        timer.doWork(9);
        CHECK(called == 1, "用例6-1: doWork 触发回调");
        // doWork 后节点被删除
        timer.tick();
        CHECK(called == 1, "用例6-2: doWork 后 tick 不再触发（已删除）");
    }

    // ---- 用例7: pop 删除堆顶 ----
    {
        HeapTimer timer;
        int a = 0, b = 0;
        timer.add(2, 0, [&]{ a++; });
        timer.add(1, 1, [&]{ b++; });
        timer.pop();  // 删堆顶（id=2, 超时 0）
        timer.tick();
        CHECK(a == 0, "用例7: pop 后堆顶回调未触发");
    }

    // ---- 用例8: clear 清空 ----
    {
        HeapTimer timer;
        timer.add(1, 0, []{});
        timer.add(2, 0, []{});
        timer.clear();
        timer.tick();
        CHECK(timer.GetNextTick() == -1, "用例8: clear 后堆为空，GetNextTick=-1");
    }

    // ---- 用例9: GetNextTick 返回堆顶剩余时间 ----
    {
        HeapTimer timer;
        timer.add(99, 10000, []{});  // 10 秒后
        int remaining = timer.GetNextTick();
        CHECK(remaining > 0 && remaining <= 10000,
              "用例9: GetNextTick 返回 0~10000 之间的值");
    }

    // ---- 用例10: 空 timer 的 GetNextTick ----
    {
        HeapTimer timer;
        CHECK(timer.GetNextTick() == (int)(size_t)-1,
              "用例10: 空堆 GetNextTick 返回最大值");
    }

    // ---- 用例11: doWork 不存在的 id ----
    {
        HeapTimer timer;
        timer.doWork(999);  // 不崩溃就是通过
        CHECK(true, "用例11: doWork 不存在的 id 不崩溃");
    }

    std::cout << "\n==== 结果: " << g_pass << " 通过, " << g_fail << " 失败 ====\n";
    return g_fail == 0 ? 0 : 1;
}

/*
在 Ubuntu 终端里编译运行:

    cd /mnt/hgfs/Project/WebServerRecur
    g++ -std=c++14 test/test_heaptimer.cpp code/timer/heaptimer.cpp \
        code/log/log.cpp code/buffer/buffer.cpp -o ~/test_heaptimer -pthread
    ~/test_heaptimer
*/
