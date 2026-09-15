#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <unordered_map>
#include <vector>
#include <mutex>
#include <fcntl.h>       // fcntl()
#include <unistd.h>      // close()
#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "epoller.h"
#include "../log/log.h"
#include "../timer/heaptimer.h"
#include "../pool/sqlconnpool.h"
#include "../pool/threadpool.h"
#include "../pool/sqlconnRAII.h"
#include "../http/httpconn.h"

class WebServer {
    public:
    WebServer(
        int port, int trigMode, int timeoutMS, bool OptLinger, 
        int sqlPort, const char* sqlUser, const  char* sqlPwd, 
        const char* dbName, int connPoolNum, int threadNum,
        bool openLog, int logLevel, int logQueSize);

    ~WebServer();
    void Start();

    private:
    bool InitSocket_(); 
    void InitEventMode_(int trigMode);
    void AddClient_(int fd, sockaddr_in addr);
  
    void DealListen_();
    void DealWrite_(HttpConn* client);
    void DealRead_(HttpConn* client);

    void SendError_(int fd, const char*info);
    void ExtentTime_(HttpConn* client);
    void CloseConn_(HttpConn* client);

    void OnTimeout_(HttpConn* client);   // 定时器到期专用入口(超时关闭的守门人)
    void MarkPending_(int fd);           // 登记一个待办 fd(worker/定时器回调都会投,加锁)
    bool HasPending_();                  // 有待办 epoll_wait 就不能长阻塞(否则重装被无限推迟)
    void DrainPending_();                // 主循环统一消费:撤残留定时器节点 / 给干完活的连接重装

    void OnRead_(HttpConn* client);
    void OnWrite_(HttpConn* client);
    void OnProcess(HttpConn* client);

    static const int MAX_FD = 65536;

    static int SetFdNonblock(int fd);

    int port_;
    bool openLinger_;
    int timeoutMS_;  
    bool isClose_;
    int listenFd_;
    char* srcDir_;
    
    uint32_t listenEvent_;
    uint32_t connEvent_;
   
    std::unique_ptr<HeapTimer> timer_;
    std::unique_ptr<ThreadPool> threadpool_;
    std::unique_ptr<Epoller> epoller_;
    std::unordered_map<int, HttpConn> users_;

    /* 待主循环处理的 fd 队列 —— 两个来源:
       ① 超时到期但连接正忙(OnTimeout_):推迟关闭,等它干完再重装定时器;
       ② 连接被关闭(CloseConn_):把这个 fd 残留的定时器节点撤掉。
       worker 线程与定时器回调都会往里投,所以加锁;消费只在主循环(见 DrainPending_)。
       为什么不让 worker 直接动 HeapTimer:见 heaptimer.cpp 里 remove() 的注释 */
    std::vector<int> pendingFds_;
    std::mutex pendingMtx_;
};   

#endif
