#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <functional>
#include <cassert>
#include <memory>
#include <exception>
#include "../log/log.h"   // 任务抛异常时记日志

class ThreadPool {
    public:
    explicit ThreadPool(size_t threadCount = 8) {
        assert(threadCount > 0);
        pool_ = std::make_shared<Pool>();
        pool_->activeCount = static_cast<int>(threadCount);   // 工人总数,析构时等他们全部退出
        for(size_t i = 0; i < threadCount; i++) {
            std::thread t([pool = pool_] {
                std::unique_lock<std::mutex> locker(pool->mtx);
                while (true) {
                    if (!pool->tasks.empty()) {
                        auto task = std::move(pool->tasks.front());
                        pool->tasks.pop();

                        locker.unlock();
                        try {
                            task();
                        } catch (const std::exception& e) {
                            // 不接住的话,异常从线程入口逃出 = 整个进程 terminate
                            LOG_ERROR("ThreadPool task exception: %s", e.what());
                        } catch (...) {
                            LOG_ERROR("ThreadPool task threw unknown exception");
                        }
                        locker.lock();
                    }
                    else if (pool->isClosed) break;
                    else pool->cond.wait(locker);
                }
                // 退出前登记:最后一个退出的工人叫醒等待中的析构函数
                if (--pool->activeCount == 0) {
                    pool->condExit.notify_all();
                }
            });
            t.detach();
        }
    }
    ThreadPool() = default;
    ThreadPool(ThreadPool&&) = default;
    ~ThreadPool() {
        if (pool_) {
            {
                std::lock_guard<std::mutex> locker(pool_->mtx);
                pool_->isClosed = true;
            }
            pool_->cond.notify_all();   // 叫醒所有工人:把队列里剩余任务干完再退

            std::unique_lock<std::mutex> locker(pool_->mtx);
            // 等工人全部退出——保证"析构返回"时任务已全部完成
            pool_->condExit.wait(locker, [this] { return pool_->activeCount == 0; });
        }
    }

    template<class F>
    bool AddTask(F&& task) {
        {
            std::lock_guard<std::mutex> locker(pool_->mtx);
            if (pool_->isClosed) {      // 已关闭:拒绝新任务,防止投进去没人执行
                return false;
            }
            pool_->tasks.emplace(std::forward<F>(task));
        }
        pool_->cond.notify_one();
        return true;
    }

    private:
    struct Pool {
        std::mutex mtx;
        std::condition_variable cond;        // 工人等任务
        std::condition_variable condExit;    // 析构等工人退完
        bool isClosed;
        int activeCount;                     // 还没退出的工人数
        std::queue<std::function<void()>> tasks;
    };
    std::shared_ptr<Pool> pool_;

};
#endif 