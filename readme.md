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
