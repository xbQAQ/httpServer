#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"
#include "mysqlpool/mysqlpool.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

extern int addfd( int epollfd, int fd, bool one_shot );
extern int removefd( int epollfd, int fd );

void addsig( int sig, void( handler )(int), bool restart = true )
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    if( restart )
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

void show_error( int connfd, const char* info )
{
    printf( "%s", info );
    send( connfd, info, strlen( info ), 0 );
    close( connfd );
}


int main( int argc, char* argv[] )
{
    if( argc < 2 )
    {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }
    const char* ip = "localhost";
    int port = atoi( argv[1] );

    addsig( SIGPIPE, SIG_IGN );

    //需要用::获取静态成员函数
    mysqlPool* mysqlpool = mysqlPool::getmysqlPool();
    unique_ptr<threadpool<http_conn>> threadPool(threadpool<http_conn>::createpool( mysqlpool, 100, 10000 ));
    http_conn* users = new http_conn[ MAX_FD ]; //任务数组
    assert( users );
    int user_count = 0;

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );
    struct linger tmp = { 1, 0 };
    setsockopt( listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof( tmp ) );

    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret >= 0 );

    ret = listen( listenfd, 5 );
    assert( ret >= 0 );

    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );
    assert( epollfd != -1 );
    addfd( epollfd, listenfd, false );
    http_conn::m_epollfd = epollfd;

    while( true )
    {
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            printf( "epoll failure\n" );
            break;
        }

        //单reactor, 负责接收客户端的连接，多线程，负责读取客户端的请求和发送响应
        for ( int i = 0; i < number; i++ )
        {
            int sockfd = events[i].data.fd;
            if( sockfd == listenfd )    //如果是listenfd
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                if ( connfd < 0 )
                {
                    printf( "errno is: %d\n", errno );
                    continue;
                }
                if( http_conn::m_user_count >= MAX_FD )     //超过了最大上限65536
                {
                    show_error( connfd, "Internal server busy" );
                    continue;
                }
                
                users[connfd].init( connfd, client_address );   //放到对应的任务数组中，并把connfd和地址传给对应的任务
                std::cout << "connected" << endl;
            }
            else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) )  //如果检测到错误或者客户端关闭
            {
                users[sockfd].close_conn();
            }
            else if( events[i].events & EPOLLIN )
            {
                if( users[sockfd].read() )  //读取客户端发过来的消息，也就是http协议
                {
                    threadPool->append( users + sockfd ); //将对应的任务加入任务列表中，让线程池里的线程去解析http协议，并写回给客户端
                }
                else
                {
                    users[sockfd].close_conn();
                }
            }
            else if( events[i].events & EPOLLOUT )  //任务的输出缓冲区有东西可以写
            {
                if( !users[sockfd].write() )    //将输出缓冲区里的数据写回给浏览器
                {
                    users[sockfd].close_conn();
                }
            }
        }
    }

    close( epollfd );
    close( listenfd );
    delete [] users;
    return 0;
}