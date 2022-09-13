#include "mysqlpool.h"
#include <string>
#include <json/json.h>
#include <thread>
#include "../locker.h"

void mysqlconn::set_character(std::string character)
{
    mysql_set_character_set(m_mysql, character.c_str());
}

mysqlconn::mysqlconn()
{
    m_mysql = mysql_init(m_mysql);
    if(m_mysql == nullptr){
        printf("mysql init error\n");
        mysql_close(m_mysql);
        exit(-1);
    }
    m_res = nullptr;
    m_row = nullptr;
}

mysqlconn::~mysqlconn()
{
    if(m_res){
        mysql_free_result(m_res);
        m_res = nullptr;
    }
    if(m_mysql){
        mysql_close(m_mysql);
        m_mysql = nullptr;
    }
}

mysqlconn::mysqlconn(std::string user, std::string password, std::string db, std::string ip, unsigned short port)
{
    mysqlconn();
    if(mysql_real_connect(m_mysql, ip.c_str(), user.c_str(), 
        password.c_str(), db.c_str(), port, nullptr, 0) == nullptr){
        printf("mysql connect error\n");
        mysql_close(m_mysql);
        exit(-1);
    }
    set_character();
}

bool mysqlconn::connect(std::string user, std::string password, std::string db, std::string ip, unsigned short port)
{
    if(mysql_real_connect(m_mysql, ip.c_str(), user.c_str(), 
        password.c_str(), db.c_str(), port, nullptr, 0) == nullptr){
        printf("mysql connect error\n");
        return false;
    }
    else{
        printf("mysql connect finished\n");
    }
    set_character();
    return true;
}

bool mysqlconn::update(std::string sql)
{
    if(mysql_query(m_mysql, sql.c_str()) != 0){
        printf("mysql update error!\n");
        return false;
    }
    return true;
}

MYSQL_RES* mysqlconn::query(std::string sql)
{
    if(m_res){
        mysql_free_result(m_res);
        m_res = nullptr;
    }
    mysql_query(m_mysql, sql.c_str());
    m_res = mysql_store_result(m_mysql);
    if(m_res == nullptr){
        printf("mysql query error: %s\n", mysql_error(m_mysql));
        return nullptr;
    }
    return m_res;
}

MYSQL_ROW mysqlconn::next()
{
    m_row = mysql_fetch_row(m_res);
    return m_row;
}

std::string mysqlconn::getUser()
{
    return m_row[1];
}

std::string mysqlconn::getpassword()
{
    return m_row[2];
}

bool mysqlconn::transaction()
{
    if(!mysql_autocommit(m_mysql, false)){
        printf("mysql autocommit error: %s\n", mysql_error(m_mysql));
        return false;
    }
    return true;
}

bool mysqlconn::commit()
{
    if(!mysql_commit(m_mysql)){
        printf("mysql commit error: %s\n", mysql_error(m_mysql));
        return false;
    }
    return true;
}

bool mysqlconn::rollback()
{
    if(!mysql_rollback(m_mysql)){
        printf("mysql rollback error: %s\n", mysql_error(m_mysql));
        return false;
    }
    return true;
}

void mysqlconn::refreshAliveTime()
{
    m_alivetime = std::chrono::steady_clock::now();
}

long long mysqlconn::getAliveTime()
{
    std::chrono::nanoseconds res = std::chrono::steady_clock::now() - m_alivetime;
    std::chrono::milliseconds millsec = std::chrono::duration_cast<std::chrono::milliseconds>(res);
    return millsec.count();
}

/*mysqlPool*/
mysqlPool* mysqlPool::getmysqlPool(){
    static mysqlPool pool;
    return &pool;
}

bool mysqlPool::loadJson()
{
    std::ifstream ifs("/root/httpserver/mysqlpool/config.json");
    if(!ifs.is_open()){
        std::cout << "config.json was not opend" << std::endl;
    }
    // else{
    //     std::cout << "config.json was opend" << std::endl;
    // }
    Json::Reader r;
    Json::Value root;
    r.parse(ifs, root);
    if(root.isObject()){
        ip = root["ip"].asString();
        port = root["port"].asInt();
        user = root["user"].asString();
        password = root["password"].asString();
        db = root["db"].asString();
        minUser = root["minUser"].asInt();
        maxUser = root["maxUser"].asInt();
        maxFreeTime = root["maxFreeTime"].asInt();
        timeout = root["timeout"].asInt();
        return true;
    }
    return false;
}

void mysqlPool::produceConnection()
{
    while(true){
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::unique_lock<std::mutex> locker(m_mutex); 
        //当前可用连接数大于最少连接数并且当前已经连接数大于最大连接数的时候，阻塞
        while(curFreeUser >= maxUser){
            produceCond.wait(locker);
        }
        for(int i = curFreeUser; i <= minUser; i++){
            addConnection();
        }
        consumerCond.notify_all();
    }
}

void mysqlPool::recycleConnection()
{
    while(true){
        //延迟1s
        std::this_thread::sleep_for(std::chrono::seconds(1));
        //当前可用连接数大于最小连接数和最大连接数的中间
        //并且当前连接数要小于最大连接数
        std::lock_guard<std::mutex> lock(m_mutex);
        while(curFreeUser > (minUser + maxUser) / 2){
            mysqlconn* conn = mysqlQ.front();
            if(conn->getAliveTime() >= maxFreeTime){
                mysqlQ.pop();
                delete conn;
            }
            else{
                break;
            }
            curFreeUser++;
            curUser--;
            consumerCond.notify_one();
        }
    }
}

mysqlPool::mysqlPool()
{
    curFreeUser = 0;
    curUser = 0;
    if(!loadJson()){
        std::cout << "load Json error!" << std::endl;
        exit(-1);
    }
    for(int i = 0; i != minUser; i++){
        addConnection();
    }
    std::thread producer(&mysqlPool::produceConnection, this);
    std::thread consumer(&mysqlPool::recycleConnection, this);
    producer.detach();
    consumer.detach();
}

mysqlPool::~mysqlPool()
{
    std::lock_guard<std::mutex> lockG(m_mutex);
    while(!mysqlQ.empty()){
        auto it = mysqlQ.front();
        mysqlQ.pop();
        delete it;
    }
}

void mysqlPool::addConnection()
{
    mysqlconn* conn = new mysqlconn;
    conn->connect(user, password, db, ip, port);
    conn->refreshAliveTime();
    mysqlQ.push(conn);
    curFreeUser++;
}

std::shared_ptr<mysqlconn> mysqlPool::getconnection()
{
    if(curUser >= maxUser){
        return nullptr;
    }
    std::unique_lock<std::mutex> locker(m_mutex);
    while(mysqlQ.empty()){
        consumerCond.wait(locker);
    }
    std::cout << "得到一个连接" << std::endl;
    std::shared_ptr<mysqlconn> conn(mysqlQ.front(), [this](mysqlconn* conn){
        std::cout << "回收了一个连接" << std::endl;
        std::lock_guard<std::mutex> lockG(m_mutex);
        conn->refreshAliveTime();
        mysqlQ.push(conn);
        curFreeUser++;
        curUser--;
    });
    printf("std::shared_ptr<mysqlconn> conn = %p\n", conn);
    mysqlQ.pop();
    produceCond.notify_one();
    curFreeUser--;
    curUser++;
    return conn;
}