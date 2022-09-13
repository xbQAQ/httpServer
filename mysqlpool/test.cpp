#include "mysqlpool.h"
#include <iostream>
#include <memory>
#include <chrono>
#include <thread>

using namespace std;

void op1(int begin, int end)
{
    for(int i = begin; i != end; i++){
        mysqlconn conn;
        conn.connect("root", "lxb592.com", "yourdb", "localhost");
        char insert[1024] = { 0 };
        sprintf(insert, "insert into user values('%d', 123456)", i);
        conn.update(insert);
    }
}

void op2(mysqlPool* pool, int begin, int end)
{
    for(int i = begin; i != end; i++){
        shared_ptr<mysqlconn> conn = pool->getconnection();
        char insert[1024] = { 0 };
        sprintf(insert, "insert into user values('%d', 123456)", i);
        conn->update(insert);
    }
}

void test1()
{
#if 0
//9619.13
    mysqlconn conn;
    conn.connect("root", "lxb592.com", "yourdb", "localhost");
    chrono::steady_clock::time_point begin = chrono::steady_clock::now();
    thread t1(op1, 0, 1000);
    thread t2(op1, 1000, 2000);
    thread t3(op1, 2000, 3000);
    thread t4(op1, 3000, 4000);
    thread t5(op1, 4000, 5000);
    thread t6(op1, 5000, 6000);
    thread t7(op1, 6000, 7000);
    thread t8(op1, 7000, 8000);
    thread t9(op1, 8000, 9000);
    thread t10(op1, 9000, 10000);
    t1.join();
    t2.join();
    t3.join();
    t4.join();
    t5.join();
    t6.join();
    t7.join();
    t8.join();
    t9.join();
    t10.join();
    chrono::steady_clock::time_point end = chrono::steady_clock::now();
    auto length = end - begin;
    cout << "多线程非连接池: " << length.count() / 1e6 << endl;
#else
//   5984.6
    mysqlPool* pool = mysqlPool::getmysqlPool();
    chrono::steady_clock::time_point begin = chrono::steady_clock::now();
    thread t1(op2, pool, 0, 1000);
    thread t2(op2, pool, 1000, 2000);
    thread t3(op2, pool, 2000, 3000);
    thread t4(op2, pool, 3000, 4000);
    thread t5(op2, pool, 4000, 5000);
    thread t6(op2, pool, 5000, 6000);
    thread t7(op2, pool, 6000, 7000);
    thread t8(op2, pool, 7000, 8000);
    thread t9(op2, pool, 8000, 9000);
    thread t10(op2, pool, 9000, 10000);
    t1.join();
    t2.join();
    t3.join();
    t4.join();
    t5.join();
    t6.join();
    t7.join();
    t8.join();
    t9.join();
    t10.join();
    chrono::steady_clock::time_point end = chrono::steady_clock::now();
    auto length = end - begin;
    cout << "多线程连接池: " << length.count() / 1e6 << endl;
#endif
}


int main()
{
// #if 0
//     //21.6327
//     chrono::steady_clock::time_point begin = chrono::steady_clock::now();
//     op1(0, 5000);
//     chrono::steady_clock::time_point end = chrono::steady_clock::now();
// #else
//     //17.1356
//     mysqlPool* pool = mysqlPool::getmysqlPool();
//     chrono::steady_clock::time_point begin = chrono::steady_clock::now();
//     op2(pool, 0, 5000);
//     chrono::steady_clock::time_point end = chrono::steady_clock::now();
// #endif
//     auto length = end - begin;
//     cout << length.count() / 1e9 << endl;
    test1();
    return 0;
}