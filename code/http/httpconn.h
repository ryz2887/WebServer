#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <sys/types.h>
#include <sys/uio.h>     // readv/writev
#include <arpa/inet.h>   // sockaddr_in
#include <stdlib.h>      // atoi()
#include <errno.h>
#include <atomic>        // busy_:worker 是否正在处理本连接(超时机制用)

#include "../log/log.h"
#include "../pool/sqlconnRAII.h"
#include "../buffer/buffer.h"
#include "httprequest.h"
#include "httpresponse.h"

class HttpConn {
    public:
    HttpConn();

    ~HttpConn();

    void init(int sockFd, const sockaddr_in& addr);

    ssize_t read(int* saveErrno);

    ssize_t write(int* saveErrno);

    void Close();

    int GetFd() const;

    int GetPort() const;

    const char* GetIP() const;
    
    sockaddr_in GetAddr() const;
    
    bool process();

    int ToWriteBytes() { 
        return iov_[0].iov_len + iov_[1].iov_len; 
    }

    bool IsKeepAlive() const {
        return request_.IsKeepAlive();
    }

    bool IsClose() const {
        return isClose_;
    }

    /* 超时关闭与 worker 的竞争保护:
       置位:DealRead_/DealWrite_ 把任务投进线程池时(排队期间也算"在途")
       清零:任务的所有出口 —— 由 webserver.cpp 里的 BusyGuard 负责(提前 return 也不漏)
       用途:定时器到期回调(OnTimeout_)看到 busy 就"不关 fd,只登记",等 worker 干完再重装定时器 */
    bool IsBusy() const { return busy_.load(); }
    void SetBusy(bool busy) { busy_.store(busy); }

    static bool isET;
    static const char* srcDir;
    static std::atomic<int> userCount;

    private:
        int fd_;
    struct  sockaddr_in addr_;

    bool isClose_;
    std::atomic<bool> busy_{false};   // 见 IsBusy() 上面的说明
    
    int iovCnt_;
    struct iovec iov_[2];
    
    Buffer readBuff_; 
    Buffer writeBuff_;

    HttpRequest request_;
    HttpResponse response_;
};

#endif