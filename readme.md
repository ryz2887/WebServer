#项目名：WebServer
基于Linux epoll+线程池的C++高性能web服务器

##功能特性
- 支持 GET/POST，静态资源 + 登录注册（MySQL 验证）
- 1000 并发 0 失败（webbench 压测 9200 req/s）

##架构图
```text
┌─────────────────────────────────────┐
│         WebServer（装配+调度）        │
│                                     │
客户端 A ──TCP──→ │ listenFd ──epoll_wait──事件来了──分发给 ↓ │
客户端 B ──TCP──→ │                                     │
客户端 C ──TCP──→ │  ┌─ 新连接？→ accept → HttpConn.init  │
                 │  ├─ 可读？  → DealRead_ →线程池→OnRead_ │
                 │  ├─ 可写？  → DealWrite_→线程池→OnWrite_│
                 │  └─ 断开？  → CloseConn_               │
                 └────────────┬────────────────────────┘
                              │
              ┌───────────────┼───────────────┐
              │               │               │
        ┌─────▼─────┐  ┌─────▼─────┐  ┌─────▼─────┐
        │ ThreadPool │  │ HeapTimer │  │ SqlConnPool│
        │ 6 线程干活  │  │ 超时踢人   │  │ MySQL 12连接│
        └───────────┘  └───────────┘  └───────────┘
              │
        ┌─────▼──────────────────────────┐
        │      HttpConn（每个连接一个）    │
        │  read → HttpRequest.parse      │
        │       → HttpResponse.MakeResp  │
        │       → write (writev)         │
        └───────────────────────────────┘
                     │
              ┌─────▼─────┐
              │ Buffer ×2  │  ← 读写缓冲
              └───────────┘
```

##技术点
- **Reactor 结构（半同步半反应堆）**：主循环只做事件分发（Deal* 层），读写等重活丢进线程池（On* 层），主循环绝不阻塞
- **epoll 边缘触发**：连接 fd 全部非阻塞 + ET + EPOLLONESHOT；ET 要求一次事件把数据读干净（循环读到 EAGAIN 为止），ONESHOT 保证同一连接同一时刻只被一个线程处理
- **定时器**：最小堆 + `unordered_map(id → 堆下标)`，定位 O(1)、调整 O(log n)；惰性删除，并与 epoll_wait 联动（等待时长 = 最近到期时间）
- **超时关闭与 worker 的竞争**：定时器到期时若该连接正被 worker 处理，不关 fd、只登记，等处理完再重新装定时器——否则慢请求会被自己的超时误杀，且新连接会复用同一个 fd 号、把 worker 手里的连接对象整个重置掉
- **连接池 + RAII**：SqlConnPool 用信号量管理 12 条 MySQL 连接；SqlConnRAII 借出即构造、析构即归还（禁拷贝，防双重归还）
- **异步日志**：BlockDeque 做生产者-消费者队列，业务线程只入队、单独线程落盘（按天/按行数切文件），业务路径不阻塞在磁盘 I/O 上
- **零拷贝发文件**：mmap 映射静态资源 + writev 一次系统调用发两段（响应头在用户态 Buffer、文件体在 mmap 区），并处理部分写（iov 基址/长度递推）
- **Buffer 三区设计**：prepend / readable / writable 读写指针分离，readv 一次读到缓冲区尾部，避免大文件多次拷贝
- **线程池的健壮性**：任务异常在 worker 内 try-catch 记日志（异常逃出线程入口 = `std::terminate` 整个进程）；析构用 `activeCount` + 条件变量等所有工人退完才返回，关闭后 AddTask 拒绝新任务
