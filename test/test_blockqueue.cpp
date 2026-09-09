// BlockDeque 模块单元测试
// 编译见文件末尾注释。每个用例打印 [PASS]/[FAIL]。
#include "../code/log/blockqueue.h"
#include <iostream>
#include <thread>
#include <chrono>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do {                       \
    if (cond) { std::cout << "[PASS] " << msg << "\n"; ++g_pass; } \
    else      { std::cout << "[FAIL] " << msg << "\n"; ++g_fail; } \
} while (0)

int main() {
    // ---- 用例1: 构造与基本查询 ----
    {
        BlockDeque<int> q(5);
        CHECK(q.empty(), "用例1: 新建队列为空");
        CHECK(!q.full(), "用例1: 新建队列不满");
        CHECK(q.size() == 0, "用例1: size=0");
        CHECK(q.capacity() == 5, "用例1: capacity=5");
    }

    // ---- 用例2: push_back + front/back ----
    {
        BlockDeque<int> q(5);
        q.push_back(10);
        q.push_back(20);
        q.push_back(30);
        CHECK(q.size() == 3, "用例2: push3个后size=3");
        CHECK(q.front() == 10, "用例2: front=10");
        CHECK(q.back() == 30, "用例2: back=30");
        CHECK(!q.empty(), "用例2: 非空");
    }

    // ---- 用例3: pop 取数据(FIFO) ----
    {
        BlockDeque<int> q(5);
        q.push_back(1);
        q.push_back(2);
        q.push_back(3);

        int val = 0;
        CHECK(q.pop(val), "用例3: pop返回true");
        CHECK(val == 1, "用例3: 第一个pop得1");
        CHECK(q.pop(val), "用例3: 第二个pop返回true");
        CHECK(val == 2, "用例3: 第二个pop得2");
        CHECK(q.size() == 1, "用例3: 还剩1个");
    }

    // ---- 用例4: push_front ----
    {
        BlockDeque<int> q(5);
        q.push_back(1);
        q.push_front(0);
        CHECK(q.front() == 0, "用例4: push_front后front=0");

        int val = 0;
        q.pop(val);
        CHECK(val == 0, "用例4: 先出push_front的0");
        q.pop(val);
        CHECK(val == 1, "用例4: 然后是1");
    }

    // ---- 用例5: clear ----
    {
        BlockDeque<int> q(5);
        q.push_back(1);
        q.push_back(2);
        q.clear();
        CHECK(q.empty(), "用例5: clear后为空");
        CHECK(q.size() == 0, "用例5: clear后size=0");
    }

    // ---- 用例6: full 检测 ----
    {
        BlockDeque<int> q(2);
        q.push_back(1);
        q.push_back(2);
        CHECK(q.full(), "用例6: 塞2个后full=true(capacity=2)");
    }

    // ---- 用例7: 超时pop(空队列等1秒) ----
    {
        BlockDeque<int> q(5);
        int val = 0;
        bool ok = q.pop(val, 1);
        CHECK(!ok, "用例7: 空队列pop超时返回false");
    }

    // ---- 用例8: 阻塞pop(子线程)被Close唤醒 ----
    {
        BlockDeque<int> q(5);
        bool thread_done = false;
        bool pop_result = true;  // 故意给true,看pop会不会改成false

        std::thread t([&]() {
            int val = 0;
            pop_result = q.pop(val);   // 队列空，阻塞
            thread_done = true;
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        q.Close();                     // 关闭 → 叫醒所有等待者
        t.join();

        CHECK(thread_done, "用例8: Close后pop线程退出");
        CHECK(!pop_result, "用例8: Close后pop返回false");
    }

    // ---- 用例9: 阻塞push(满了等空位，被pop唤醒) ----
    {
        BlockDeque<int> q(2);
        q.push_back(1);
        q.push_back(2);                // 满了

        bool push_done = false;
        std::thread t([&]() {
            q.push_back(3);            // 阻塞等空位
            push_done = true;
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        int val = 0;
        q.pop(val);
        CHECK(val == 1, "用例9: pop出第一个元素1");

        t.join();
        CHECK(push_done, "用例9: 生产者被唤醒后push成功");
        CHECK(q.size() == 2, "用例9: 还剩2个(2和3)");

        q.pop(val);
        CHECK(val == 2, "用例9: 第二个是2");
        q.pop(val);
        CHECK(val == 3, "用例9: 第三个是3(阻塞塞入的)");
    }

    std::cout << "\n==== 结果: " << g_pass << " 通过, " << g_fail << " 失败 ====\n";
    return g_fail == 0 ? 0 : 1;
}

/*
在 Ubuntu 终端里编译运行:

    cd /mnt/hgfs/Project/WebServerRecur
    g++ -std=c++14 test/test_blockqueue.cpp -o ~/test_blockqueue -pthread
    ~/test_blockqueue
*/
