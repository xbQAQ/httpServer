#ifndef MYSQLPOOL_H
#define MYSQLPOOL_H

#include "../locker.h"
#include <mysql/mysql.h>
#include <queue>
#include <string>
#include <iostream>
#include <fstream>
#include <cstring>
#include <stdio.h>
#include <unistd.h>
#include <memory>
#include <json/json.h>
#include <chrono>
#include <mutex>
#include <condition_variable>

class mysqlconn{
public:
    //初始化数据库连接
    mysqlconn();
    //销毁数据库资源
    ~mysqlconn();
    //初始化数据库连接并且连接数据库和设置编码
    mysqlconn(std::string user, std::string password, std::string db, std::string ip, unsigned short port = 3306);
    //连接数据库
    bool connect(std::string user, std::string password, std::string db, std::string ip, unsigned short port = 3306);
    //增删改
    bool update(std::string sql);
    //查
    MYSQL_RES* query(std::string sql);
    //获得下一行
    MYSQL_ROW next();
    //获得当前行特定字段值
    std::string getUser();
    std::string getpassword();
    //开启事务
    bool transaction();
    //事务提交
    bool commit();
    //回滚
    bool rollback();
    //刷新存活时长
    void refreshAliveTime();    
    //计算连接存活的总时长
    long long getAliveTime();   
private:
    //设置编码
    void set_character(std::string character = "utf8");

private:
    MYSQL* m_mysql = nullptr;
    MYSQL_RES* m_res = nullptr;
    MYSQL_ROW m_row = nullptr;
    std::chrono::steady_clock::time_point m_alivetime; 
};

class mysqlPool{
public:
    static mysqlPool* getmysqlPool();
    std::shared_ptr<mysqlconn> getconnection();
    mysqlPool(const mysqlPool&) = delete;
    mysqlPool& operator=(const mysqlPool&) = delete;
    ~mysqlPool();

private:
    bool loadJson();
    void addConnection();
    mysqlPool();
    void produceConnection();   //生产者线程的函数
    void recycleConnection();   //回收线程的函数
    
private:
    std::string user;
    std::string password;
    std::string db;
    std::string ip;
    unsigned short port;

    int curUser;        //当前的连接数
    int curFreeUser;    //当前可用连接数
    int maxUser;        //最大连接数
    int minUser;        //最少连接数
    int timeout;        //超时时长
    int maxFreeTime;    //最大空闲时长，当数据库连接超过一定时间之后，就会被归还给数据库连接池
    std::queue<mysqlconn*> mysqlQ;      //数据库连接队列
    
    std::mutex m_mutex; //互斥锁
    std::condition_variable produceCond;    //生产者条件变量
    std::condition_variable consumerCond;   //消费者条件变量
};

#endif