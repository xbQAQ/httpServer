#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>
#include <unordered_map>
#include <iostream>
#include <string>
#include <memory>
#include "mysqlpool/mysqlpool.h"
#include "locker.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 2000;    //文件名长度
    static const int READ_BUFFER_SIZE = 2048;   //读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 2048;  //写缓冲区大小
    enum METHOD { GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH };
    //主状态机的可能状态：当前正在分析请求行，当前正在分析头部字段，当前正在分析请求体
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    //从状态机的可能状态，即行的读取状态：读取到一个完整的行、行出错、行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };
    //NO_REQUEST表示请求不完整，需要继续读取客户数据
    //GET_REQUEEST表示获得一个完整的客户请求
    //BAD_REQUEST表示客户请求有语法错误
    //FORBIDDEN_REQUEST表示客户对资源没有足够的访问权限
    //FILE_REQUEST表示请求一个文件
    //INTERNAL_ERROR表示服务器内部错误
    //CLOSED_CONNECTION表示客户端已经关闭连接了
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, 
                    FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION, DIR_REQUEST };

public:
    http_conn(){}
    ~http_conn(){}

public:
    void init( int sockfd, const sockaddr_in& addr );
    void close_conn( bool real_close = true );
    void process();
    bool read();
    bool write();
    bool getconnection(std::shared_ptr<mysqlconn> conn);

private:
    void init();
    HTTP_CODE process_read();
    bool process_write( HTTP_CODE ret );

    HTTP_CODE parse_request_line( char* text );
    HTTP_CODE parse_headers( char* text );
    HTTP_CODE parse_content( char* text );
    HTTP_CODE do_request();
    char* get_line() { return m_read_buf + m_start_line; }
    LINE_STATUS parse_line();

    void unmap();
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;

private:
    int m_sockfd;   //该HTTP连接的socket和对方的地址
    sockaddr_in m_address;

    char m_read_buf[ READ_BUFFER_SIZE ];    //读缓冲区
    int m_read_idx;         //标识读缓冲区中已经读入的客户数据的最后一个字节的下一个位置
    int m_checked_idx;      //当前正在分析的字符在读缓冲区的位置
    int m_start_line;       //当前正在解析的行的起始位置
    char m_write_buf[ WRITE_BUFFER_SIZE ];  //写缓冲区
    int m_write_idx;        //写缓冲区待发送的字节数

    CHECK_STATE m_check_state;  //主状态机当前所处的状态
    METHOD m_method;        //请求方法

    char m_real_file[ FILENAME_LEN ];       //客户请求的目标文件的完整路径，其内容等于/root + m_url, /root是根目录
    char* m_url;                //url
    char* m_version;            //版本信息
    char* m_host;               //地址
    int m_content_length;       //内容体长度
    bool m_linger;              //是否长连接
    std::string m_string;       //body内容
    int cgi;                    //判断是否post请求

    char* m_file_address;       //客户请求的目录文件被mmap到内存的起始位置
    struct stat m_file_stat;    //目标文件的状态。通过它我们可以判断文件是否存在，是否为目录，是否可读，并获取文件大小等信息
    struct iovec m_iv[2];       //采用writev来执行写操作，所以定义下面两个成员
    int m_iv_count;             //m_iv_count表示写内存块的数量
    int bytes_to_send;          //将要发送的数据大小
    int bytes_have_send;        //即将发送的数据大小
    std::shared_ptr<mysqlconn> m_connection;  //数据库连接
};

#endif
