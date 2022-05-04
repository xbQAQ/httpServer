#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include <memory>
#include "locker.h"

using namespace std;

template< typename T >
class threadpool
{
public:
    threadpool(threadpool<T>&&) = delete;
    threadpool(threadpool<T>&)  = delete;
    
    ~threadpool();
    bool append( T* request );
    //单例模式
    static shared_ptr<threadpool<T>> createpool(int thread_number = 8, int max_requests = 10000);

private:
    //构造函数
    threadpool( int thread_number = 8, int max_requests = 10000 );
    static void* worker( void* arg );
    void run();

private:
    int m_thread_number;
    int m_max_requests;
    pthread_t* m_threads;
    std::list< T* > m_workqueue;
    locker m_queuelocker;
    sem m_queuestat;
    bool m_stop;
    static shared_ptr<threadpool<T>> instance;
};

template <typename T>
shared_ptr<threadpool<T>> threadpool<T>::instance = nullptr;

template< typename T >
threadpool< T >::threadpool( int thread_number, int max_requests ) : 
        m_thread_number( thread_number ), m_max_requests( max_requests ), m_stop( false ), m_threads( NULL )
{
    if( ( thread_number <= 0 ) || ( max_requests <= 0 ) )
    {
        throw std::exception();
    }

    m_threads = new pthread_t[ m_thread_number ];
    if( ! m_threads )
    {
        throw std::exception();
    }

    for ( int i = 0; i < thread_number; ++i )
    {
        printf( "create the %dth thread\n", i );
        //向worker函数中传递 this指针，因为worker函数是静态成员函数，要使用非静态成员变量可以通过this进行访问
        if( pthread_create( m_threads + i, NULL, worker, this ) != 0 )  
        {
            delete [] m_threads;
            throw std::exception();
        }
        if( pthread_detach( m_threads[i] ) )
        {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template< typename T >
threadpool< T >::~threadpool()
{
    delete [] m_threads;
    m_stop = true;
}

template< typename T >
bool threadpool< T >::append( T* request )
{
    m_queuelocker.lock();   //锁住整个线程池
    if ( m_workqueue.size() > m_max_requests )
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back( request );   //向任务队列中加入一个任务
    m_queuelocker.unlock();
    m_queuestat.post();     //向阻塞在工作队列的线程发送信号
    return true;
}

template< typename T >
void* threadpool< T >::worker( void* arg )
{
    threadpool* pool = ( threadpool* )arg;
    pool->run();
    return pool;
}

template< typename T >
void threadpool< T >::run()
{
    while ( ! m_stop )
    {
        m_queuestat.wait(); //等待工作队列有任务来
        m_queuelocker.lock();
        if ( m_workqueue.empty() )
        {
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if ( ! request )
        {
            continue;
        }
        request->process();
    }
}

//默认参数在函数声明和定义只写一次
template<typename T>
shared_ptr<threadpool<T>> threadpool<T>::createpool(int thread_number, int max_requests)
{
    if(instance == NULL){
        try{
            //用make_shared会没有权限访问，可以通过reset进行内存分配
            instance.reset(new threadpool<T>(thread_number, max_requests));
        }
        catch(...) {
            return nullptr;
        }
    }   
    return instance;
}

#endif