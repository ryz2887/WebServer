#include "webserver.h"

using namespace std;

namespace {
/* 任务期间的 busy 守卫:构造置位、析构清零 —— 覆盖本任务的**所有出口**
   (包括提前 return)。比"在每个 return 前手写 SetBusy(false)"可靠:漏一行 = busy 卡住
   = 这个连接再也关不掉(fd 泄漏 + 待办队列无限增长)。
   注意:任务内部调用 CloseConn_ 时 busy 仍为 true,但 CloseConn_ 不检查 busy
   (检查 busy 的只有定时器入口 OnTimeout_),所以"worker 主动关闭"不会被误推迟。 */
struct BusyGuard {
    HttpConn* conn_;
    explicit BusyGuard(HttpConn* conn) : conn_(conn) { conn_->SetBusy(true); }
    ~BusyGuard() { conn_->SetBusy(false); }
    BusyGuard(const BusyGuard&) = delete;
    BusyGuard& operator=(const BusyGuard&) = delete;
};
}

WebServer::WebServer(
            int port, int trigMode, int timeoutMS, bool OptLinger,
            int sqlPort, const char* sqlUser, const  char* sqlPwd,
            const char* dbName, int connPoolNum, int threadNum,
            bool openLog, int logLevel, int logQueSize):
            port_(port), openLinger_(OptLinger), timeoutMS_(timeoutMS), isClose_(false),
            timer_(new HeapTimer()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller())
    {
    srcDir_ = getcwd(nullptr, 256);
    assert(srcDir_);
    strncat(srcDir_, "/resources/", 16);
    HttpConn::userCount = 0;
    HttpConn::srcDir = srcDir_;
    SqlConnPool::Instance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    InitEventMode_(trigMode);
    if(!InitSocket_()) { isClose_ = true;}

    if(openLog) {
        Log::Instance()->init(logLevel, "./log", ".log", logQueSize);
        if(isClose_) { LOG_ERROR("========== Server init error!=========="); }
        else {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger? "true":"false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                            (listenEvent_ & EPOLLET ? "ET": "LT"),
                            (connEvent_ & EPOLLET ? "ET": "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
}

WebServer::~WebServer() {
    close(listenFd_);
    isClose_ = true;
    free(srcDir_);
    SqlConnPool::Instance()->ClosePool();
}

void WebServer::InitEventMode_(int trigMode) {
    listenEvent_ = EPOLLRDHUP;
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP;
    switch (trigMode)
    {
    case 0:
        break;
    case 1:
        connEvent_ |= EPOLLET;
        break;
    case 2:
        listenEvent_ |= EPOLLET;
        break;
    case 3:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    default:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    }
    HttpConn::isET = (connEvent_ & EPOLLET);
}

void WebServer::Start() {
    int timeMS = -1;  /* epoll wait timeout == -1 无事件将阻塞 */
    if(!isClose_) { LOG_INFO("========== Server start =========="); }
    while(!isClose_) {
        if(timeoutMS_ > 0) {
            timeMS = timer_->GetNextTick();
        }
        /* 有待办时最多阻塞 100ms:否则等堆里没有别的定时器时,-1 会让 epoll_wait
           一直睡下去,被推迟的连接(比如 keep-alive 后没人再发数据)就永远等不到重装定时器 */
        if(HasPending_() && (timeMS < 0 || timeMS > 100)) {
            timeMS = 100;
        }
        int eventCnt = epoller_->Wait(timeMS);
        /* 先消费待办(必须在事件分发**之前**):撤残留定时器节点要赶在
           "这个 fd 被本轮 DealListen_ 的新连接复用"前面 */
        DrainPending_();
        for(int i = 0; i < eventCnt; i++) {
            /* 处理事件 */
            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEvents(i);
            if(fd == listenFd_) {
                DealListen_();
            }
            else if(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                assert(users_.count(fd) > 0);
                CloseConn_(&users_[fd]);
            }
            else if(events & EPOLLIN) {
                assert(users_.count(fd) > 0);
                DealRead_(&users_[fd]);
            }
            else if(events & EPOLLOUT) {
                assert(users_.count(fd) > 0);
                DealWrite_(&users_[fd]);
            } else {
                LOG_ERROR("Unexpected event");
            }
        }
    }
}

void WebServer::SendError_(int fd, const char*info) {
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if(ret < 0) {
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd);
}

void WebServer::CloseConn_(HttpConn* client) {
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    epoller_->DelFd(client->GetFd());
    client->Close();
    /* 交给主循环把这个 fd 残留的定时器节点撤掉(不能在这里直接调 timer_->remove:
       这里可能跑在 worker 线程上,而 HeapTimer 不是线程安全的) */
    MarkPending_(client->GetFd());
}

void WebServer::MarkPending_(int fd) {
    std::lock_guard<std::mutex> locker(pendingMtx_);
    pendingFds_.push_back(fd);
}

bool WebServer::HasPending_() {
    std::lock_guard<std::mutex> locker(pendingMtx_);
    return !pendingFds_.empty();
}

/* 定时器到期 —— 超时关闭的唯一入口。
   关键:如果这个连接正被 worker 处理(或任务还在池子里排队),**绝不能关 fd**:
   关了之后新连接 accept 会复用同一个 fd 号,users_[fd].init() 会把 worker 手里的
   同一个对象重置掉 —— worker 干完活的 writev 就把 A 的响应发到了 B 身上(跨连接串响应)。
   正确做法:只登记,等它不忙了再由主循环重新装定时器。 */
void WebServer::OnTimeout_(HttpConn* client) {
    assert(client);
    if(client->IsBusy()) {
        LOG_DEBUG("Client[%d] timeout but busy, defer close", client->GetFd());
        MarkPending_(client->GetFd());
        return;
    }
    CloseConn_(client);
}

/* 主循环统一消费待办 fd(只在 epoll_wait 之后、事件分发之前调用):
   ① 连接已关  → 撤掉它可能残留的定时器节点(否则残节点到期还会空跑一次回调)
   ② 还开着且忙 → 留到下一轮(worker 还没干完)
   ③ 还开着不忙 → 重新装定时器(刚才的超时被推迟过,现在它空闲了,重新计时) */
void WebServer::DrainPending_() {
    std::vector<int> fds;
    {
        std::lock_guard<std::mutex> locker(pendingMtx_);
        if(pendingFds_.empty()) { return; }
        fds.swap(pendingFds_);
    }
    for(size_t i = 0; i < fds.size(); i++) {
        const int fd = fds[i];
        auto it = users_.find(fd);
        if(it == users_.end() || it->second.IsClose()) {
            timer_->remove(fd);                 // 幂等:没有节点也无所谓
        }
        else if(it->second.IsBusy()) {
            MarkPending_(fd);                   // 还没干完,下一轮再看
        }
        else if(timeoutMS_ > 0) {
            /* 用 add 而不是 adjust:adjust 的前提是"节点还在"(heaptimer.cpp 里有 assert),
               而这一轮恰恰可能是"节点已被 tick 消费掉"的情况;
               add 的语义是"存在则覆盖、不存在则新建",两种情况都对 */
            timer_->add(fd, timeoutMS_, std::bind(&WebServer::OnTimeout_, this, &it->second));
        }
    }
}

void WebServer::AddClient_(int fd, sockaddr_in addr) {
    assert(fd > 0);
    users_[fd].init(fd, addr);
    if(timeoutMS_ > 0) {
        /* 回调用 OnTimeout_ 而不是 CloseConn_:OnTimeout_ 是"超时关闭的守门人",
           发现连接正忙时会推迟关闭(见它的注释) */
        timer_->add(fd, timeoutMS_, std::bind(&WebServer::OnTimeout_, this, &users_[fd]));
    }
    epoller_->AddFd(fd, EPOLLIN | connEvent_);
    SetFdNonblock(fd);
    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}

void WebServer::DealListen_() {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    do {
        int fd = accept(listenFd_, (struct sockaddr *)&addr, &len);
        if(fd <= 0) { return;}
        else if(HttpConn::userCount >= MAX_FD) {
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }
        AddClient_(fd, addr);
    } while(listenEvent_ & EPOLLET);
}

void WebServer::DealRead_(HttpConn* client) {
    assert(client);
    ExtentTime_(client);
    /* 派发即置位 busy:任务还在池子里排队时也算"在途",否则这段排队时间正好被
       超时命中 → 又会踩同一个坑。清零由任务内的 BusyGuard 负责 */
    client->SetBusy(true);
    if(!threadpool_->AddTask(std::bind(&WebServer::OnRead_, this, client))) {
        client->SetBusy(false);      // 投递失败(线程池已关闭):别把 busy 卡住
    }
}

void WebServer::DealWrite_(HttpConn* client) {
    assert(client);
    ExtentTime_(client);
    client->SetBusy(true);
    if(!threadpool_->AddTask(std::bind(&WebServer::OnWrite_, this, client))) {
        client->SetBusy(false);
    }
}

void WebServer::ExtentTime_(HttpConn* client) {
    assert(client);
    /* 用 add 而不是 adjust:adjust 的前提是"节点还在"(heaptimer.cpp 里有 assert),
       但在"超时曾被推迟"的新时序下,节点可能已经先被 tick 消费掉了;
       add 的语义是"存在则覆盖、不存在则新建",两种情况都对 */
    if(timeoutMS_ > 0) {
        timer_->add(client->GetFd(), timeoutMS_,
                    std::bind(&WebServer::OnTimeout_, this, &users_[client->GetFd()]));
    }
}

void WebServer::OnRead_(HttpConn* client) {
    assert(client);
    BusyGuard guard(client);        // 本任务的所有出口都会清零 busy
    int ret = -1;
    int readErrno = 0;
    ret = client->read(&readErrno);
    if(ret <= 0 && readErrno != EAGAIN) {
        CloseConn_(client);
        return;
    }
    OnProcess(client);
}

void WebServer::OnProcess(HttpConn* client) {
    if(client->process()) {
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
    } else {
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
    }
}

void WebServer::OnWrite_(HttpConn* client) {
    assert(client);
    BusyGuard guard(client);        // 写大文件也可能慢,同样要挡住超时关闭
    int ret = -1;
    int writeErrno = 0;
    ret = client->write(&writeErrno);
    if(client->ToWriteBytes() == 0) {
        /* 传输完成 */
        if(client->IsKeepAlive()) {
            OnProcess(client);
            return;
        }
    }
    else if(ret < 0) {
        if(writeErrno == EAGAIN) {
            /* 继续传输 */
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
            return;
        }
    }
    CloseConn_(client);
}

/* Create listenFd */
bool WebServer::InitSocket_() {
    int ret;
    struct sockaddr_in addr;
    if(port_ > 65535 || port_ < 1024) {
        LOG_ERROR("Port:%d error!",  port_);
        return false;
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);
    struct linger optLinger = { 0 };
    if(openLinger_) {
        /* 优雅关闭: 直到所剩数据发送完毕或超时 */
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }

    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if(listenFd_ < 0) {
        LOG_ERROR("Create socket error!", port_);
        return false;
    }

    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if(ret < 0) {
        close(listenFd_);
        LOG_ERROR("Init linger error!", port_);
        return false;
    }

    int optval = 1;
    /* 端口复用 */
    /* 只有最后一个套接字会正常接收数据。 */
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    if(ret == -1) {
        LOG_ERROR("set socket setsockopt error !");
        close(listenFd_);
        return false;
    }

    ret = bind(listenFd_, (struct sockaddr *)&addr, sizeof(addr));
    if(ret < 0) {
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    ret = listen(listenFd_, 6);
    if(ret < 0) {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFd_);
        return false;
    }
    ret = epoller_->AddFd(listenFd_,  listenEvent_ | EPOLLIN);
    if(ret == 0) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }
    SetFdNonblock(listenFd_);
    LOG_INFO("Server port:%d", port_);
    return true;
}

int WebServer::SetFdNonblock(int fd) {
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}