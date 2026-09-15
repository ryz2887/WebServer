#ifndef SQLCONNRAII_H
#define SQLCONNRAII_H
#include "sqlconnpool.h"

class SqlConnRAII {
    public:
    SqlConnRAII(MYSQL** sql, SqlConnPool *connpool) {
        assert(connpool);
        *sql = connpool->GetConn();
        sql_ = *sql;
        connpool_ = connpool;
    }
    
    // 禁拷贝:防止两个对象持有同一连接,析构时双重归还
    SqlConnRAII(const SqlConnRAII&) = delete;
    SqlConnRAII& operator=(const SqlConnRAII&) = delete;

    ~SqlConnRAII() {
        if(sql_) { connpool_->FreeConn(sql_); }
    }
    
    private:
    MYSQL *sql_;
    SqlConnPool* connpool_;
};

#endif