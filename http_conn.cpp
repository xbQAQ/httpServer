#include "http_conn.h"

const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";
const char* doc_root = "/root/httpserver/html";    //设置根目录

int setnonblocking( int fd )
{
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}

void addfd( int epollfd, int fd, bool one_shot )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if( one_shot )
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}

void removefd( int epollfd, int fd )
{
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
    close( fd );
}

void modfd( int epollfd, int fd, int ev )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

std::unordered_map<std::string, std::string> users;    //存储账号和密码
locker mutex;

bool http_conn::getconnection(std::shared_ptr<mysqlconn> conn)
{
    m_connection = conn;
    if(m_connection == nullptr){
        return false;
    }
    m_connection->query("select * from user");
    while(m_connection->next()){
        // std::cout << m_connection->getUser() << " " << m_connection->getpassword() << std::endl;
        users[m_connection->getUser()] = m_connection->getpassword();
    }
    return true;
}

void http_conn::close_conn( bool real_close )
{
    if( real_close && ( m_sockfd != -1 ) )
    {
        //modfd( m_epollfd, m_sockfd, EPOLLIN );
        removefd( m_epollfd, m_sockfd );
        m_sockfd = -1;
        m_user_count--;
    }
}

//初始化文件描述符和地址
void http_conn::init( int sockfd, const sockaddr_in& addr )
{
    m_sockfd = sockfd;
    m_address = addr;
    int error = 0;
    socklen_t len = sizeof( error );
    getsockopt( m_sockfd, SOL_SOCKET, SO_ERROR, &error, &len );
    int reuse = 1;
    setsockopt( m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    addfd( m_epollfd, sockfd, true );
    m_user_count++;

    init();
}

//初始化各项成员，包括方法、url、读写缓冲区、读写起始位置、已经检查的起始位置
void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    memset( m_read_buf, '\0', READ_BUFFER_SIZE );       //初始化读缓冲
    memset( m_write_buf, '\0', WRITE_BUFFER_SIZE );     //初始化写缓冲
    memset( m_real_file, '\0', FILENAME_LEN );          //初始化路径名
}

//子状态机，读取行
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    //m_checked_idx指向m_read_buf(读缓冲区)中当前正在分析的字节，m_read_idx指向客户数据尾部的下一个字节
    //m_read_buf中0~m_checked_idx字节都已分析完毕，第m_checked_idx~(m_read_idx - 1)由下面循环挨个分析
    for ( ; m_checked_idx < m_read_idx; ++m_checked_idx )
    {
        temp = m_read_buf[ m_checked_idx ];
        if ( temp == '\r' )     //差个\n
        {
            if ( ( m_checked_idx + 1 ) == m_read_idx )
            {
                return LINE_OPEN;   //需要继续读
            }
            else if ( m_read_buf[ m_checked_idx + 1 ] == '\n' ) //下一个字符是\n，那么就读到完整的一行
            {
                m_read_buf[ m_checked_idx++ ] = '\0';   //  \r->\0
                m_read_buf[ m_checked_idx++ ] = '\0';   //  \n->\0
                return LINE_OK;     //已经读了一行
            }

            return LINE_BAD;        //读取失败
        }
        else if( temp == '\n' )     
        {
            if( ( m_checked_idx > 1 ) && ( m_read_buf[ m_checked_idx - 1 ] == '\r' ) )  //看上一个是不是\r, 是就读取成功，并设置\0
            {
                m_read_buf[ m_checked_idx-1 ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;        //读取失败
        }
    }

    return LINE_OPEN;       //继续读
}

//解析请求行
http_conn::HTTP_CODE http_conn::parse_request_line( char* text )
{
    //GET /abc.txt HTTP/1.0
    m_url = strpbrk( text, " \t" );     //text中找到第一个包含\t的位置
    //如果请求行没有空白字符或者\t，则HTTP请求有问题
    if ( ! m_url )
    {
        std::cout << "m_url error" << std::endl;
        return BAD_REQUEST;
    }
    //GET\0/abc.txt HTTP/1.0
    *m_url++ = '\0';

    char* method = text;
    if ( strcasecmp( method, "GET" ) == 0 ){
        m_method = GET;     //方法设置为GET
    }
    else if( strcasecmp( method, "POST" ) == 0 ){
        m_method = POST;    //方法设置为POST
        cgi = 1;
    }
    else if( strcasecmp( method, "PUT" ) == 0 ){
        m_method = PUT; //方法设置为PUT
    }
    else if( strcasecmp( method, "DELETE" ) == 0 ){
        m_method = DELETE; //方法设置为DELETE
    }
    else{
        std::cout << "m_method error" << std::endl;
        return BAD_REQUEST;
    }
    
    //GET\0/abc.txt HTTP/1.0
    m_url += strspn( m_url, " \t" );
    m_version = strpbrk( m_url, " \t" );
    if ( ! m_version )
    {
        std::cout << "m_version error" << std::endl;
        return BAD_REQUEST;
    }
    //GET\0/abc.txt\0HTTP/1.1
    *m_version++ = '\0';
    //m_version第一个不在" \t"中出现的下标，也就是版本长度
    m_version += strspn( m_version, " \t" );        
    //仅支持HTTP/1.1
    if ( strcasecmp( m_version, "HTTP/1.1" ) != 0 )
    {
        std::cout << "not HTTP/1.1 error" << std::endl;
        return BAD_REQUEST;
    }

    //检查URL是否合法
    if ( strncasecmp( m_url, "http://", 7 ) == 0 )
    {
        m_url += 7;
        m_url = strchr( m_url, '/' );
    }

    if ( ! m_url || m_url[ 0 ] != '/' )
    {
        std::cout << "not / begin error" << std::endl;
        return BAD_REQUEST;
    }
    
    //HTTP请求行处理完成，状态转移到头部字段的解析
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

//解析请求头
http_conn::HTTP_CODE http_conn::parse_headers( char* text )
{
    //遇到空行，表示头部字段解析完毕
    if( text[ 0 ] == '\0' )
    {
        //如果HTTP请求体有消息体，则还需要读取m_content_length字节的消息体，状态机转移到CHECK_STATE_CONTENT状态
        if ( m_content_length != 0 )
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        //否则我们已经读取到了一个完整的HTTP请求
        return GET_REQUEST;
    }
    else if ( strncasecmp( text, "Connection:", 11 ) == 0 )
    {
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 )
        {
            m_linger = true;
        }
    }
    else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 )
    {
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol( text );
    }
    else if ( strncasecmp( text, "Host:", 5 ) == 0 )
    {
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    }
    else
    {
        //printf( "oop! unknow header %s\n", text );
    }

    return NO_REQUEST;

}

//解析请求体
http_conn::HTTP_CODE http_conn::parse_content( char* text )
{
    //这里没有真正的解析请求体，而是判断它是否完整的读入了
    if ( m_read_idx >= ( m_content_length + m_checked_idx ) )
    {
        text[ m_content_length ] = '\0';
        m_string = text;
        return GET_REQUEST;
    }

    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    //主状态机
    while ( ( ( m_check_state == CHECK_STATE_CONTENT ) && ( line_status == LINE_OK  ) )
                || ( ( line_status = parse_line() ) == LINE_OK ) )  //读一行
    {
        text = get_line();
        m_start_line = m_checked_idx;
        printf( "got 1 http line: %s\n", text );

        switch ( m_check_state )
        {
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line( text );
                if ( ret == BAD_REQUEST )
                {
                    printf("302 BAD_REQUEST\n");
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:    //如果是检查请求头
            {
                ret = parse_headers( text );
                if ( ret == BAD_REQUEST )
                {
                    printf("311 BAD_REQUEST\n");
                    return BAD_REQUEST;
                }
                else if ( ret == GET_REQUEST )
                {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content( text );
                if ( ret == GET_REQUEST )
                {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }

    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    std::cout << "do_request" << std::endl;
    strcpy( m_real_file, doc_root );
    int len = strlen( doc_root );
    // std::cout << "url: " << m_url << std::endl;
    // std::cout << "m_string" << m_string << std::endl;

    //默认页面
    if(strcmp(m_url, "/") == 0){
        char *m_real_url = (char *)malloc(sizeof(char) * 200);
        strcpy(m_real_url, "/judge.html");
        strncpy(m_real_file + len, m_real_url, strlen(m_real_url));
        free(m_real_url);
    }

    //m_url是/isLog, 要取得isLog, 只需要将p指向m_url的下一个字节
    char* p = m_url + 1;

    //如果是注册或者登录的话, post方式
    if(cgi == 1 && (strcasecmp(p, "log") == 0 || strcasecmp(p, "register") == 0)){
        // std::cout << "cgi" << std::endl;   
        char user[100] = {0};
        char password[100] = {0};

        //user=123456&password=123456
        int i;
        for(i = 5; m_string[i] != '&'; i++){
            user[i - 5] = m_string[i];
        }
        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';
        // std::cout << user << " " << password << std::endl;
        
        if(strcasecmp(p, "register") == 0){
            char *m_real_url = (char *)malloc(sizeof(char) * 200);
            char sql[200];
            memset(sql, 0, sizeof(sql));
            sprintf(sql, "insert into user(username, passwd) values (\'%s\', \'%s\');", user, password);
            // std::cout << sql << std::endl;
            if(users.find(user) == users.end()){
                mutex.lock(); 
                bool flag = m_connection->update(sql);
                // std::cout << flag << std::endl;
                users.insert({user, password});
                mutex.unlock();
                if(flag){
                    //如果数据库没有当前用户，则注册，并跳到起始页面
                    strcpy(m_real_url, "/judge.html");
                }
                else{
                    //已经有当前用户，跳到重新注册
                    strcpy(m_real_url, "/registerError.html");
                }
            }
            else{
                strcpy(m_real_url, "/registerError.html");
            }
            strncpy(m_real_file + len, m_real_url, strlen(m_real_url));
            free(m_real_url);
        }
        else{
            //查询是否有当前用户，如果没有，则登陆失败
            //如果有当前用户，则跳到欢迎页面
            char *m_real_url = (char *)malloc(sizeof(char) * 200);
            if(users.find(user) != users.end() && users[user] == password){
                strcpy(m_real_url, "/404.html");
            }
            else{
                strcpy(m_real_url, "/logError.html");
            }
            strncpy(m_real_file + len, m_real_url, strlen(m_real_url));
            free(m_real_url);
        }
    }
    //转到注册页面
    else if((strcasecmp(p, "isRegister")) == 0){
        // std::cout << "isRegister" << std::endl;
        char *m_real_url = (char *)malloc(sizeof(char) * 200);
        strcpy(m_real_url, "/register.html");
        strncpy(m_real_file + len, m_real_url, strlen(m_real_url));
        free(m_real_url);
    }
    //转到登录页面
    else if((strcasecmp(p, "isLog")) == 0){
        // std::cout << "isLog" << std::endl;
        char *m_real_url = (char *)malloc(sizeof(char) * 200);
        strcpy(m_real_url, "/log.html");
        strncpy(m_real_file + len, m_real_url, strlen(m_real_url));
        free(m_real_url);
    }

    if ( stat( m_real_file, &m_file_stat ) < 0 )    //判断文件类型
    {
        return NO_RESOURCE;
    }

    if ( ! ( m_file_stat.st_mode & S_IROTH ) )      //如果权限不能访问
    {
        return FORBIDDEN_REQUEST;
    }

    if ( S_ISDIR( m_file_stat.st_mode ) )           //如果是目录
    {
        //暂时没办法访问目录
        // std::cout << "isDir" << std::endl;
        return BAD_REQUEST;
    }

    // std::cout << m_real_file << std::endl;
    int fd = open( m_real_file, O_RDONLY );
    //将指定文件的内容映射到mmap，由于是MAP_PRIVATE，所以不会更改源文件
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd );
    return FILE_REQUEST;
}

void http_conn::unmap()
{
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;
    }
}

//从链接的socket中一次性读取数据
bool http_conn::read()
{
    if( m_read_idx >= READ_BUFFER_SIZE )
    {
        return false;
    }

    int bytes_read = 0;
    while( true )
    {
        bytes_read = recv( m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0 );
        if ( bytes_read == -1 )
        {
            if( errno == EAGAIN || errno == EWOULDBLOCK )
            {
                break;
            }
            return false;
        }
        else if ( bytes_read == 0 )
        {
            return false;
        }

        m_read_idx += bytes_read;
    }
    return true;
}

//向链接的socket输出写缓冲的东西
bool http_conn::write()
{
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if ( bytes_to_send == 0 )
    {
        modfd( m_epollfd, m_sockfd, EPOLLIN );
        init();
        return true;
    }

    while( 1 )
    {
        //分开写
        temp = writev( m_sockfd, m_iv, m_iv_count );
        if ( temp <= -1 )
        {
            if( errno == EAGAIN )
            {
                modfd( m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_send += temp;
        if ( bytes_to_send <= bytes_have_send )
        {
            unmap();
            if( m_linger )
            {
                init();
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return true;
            }
            else
            {
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return false;
            } 
        }
    }
}

//在write_buf中添加响应行/响应头/响应体
bool http_conn::add_response( const char* format, ... )
{
    if( m_write_idx >= WRITE_BUFFER_SIZE )
    {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    //拼接目录和文件名
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) )
    {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}

//添加状态行
bool http_conn::add_status_line( int status, const char* title )
{
    //拼接写回去的响应请求行
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

//添加消息头
bool http_conn::add_headers( int content_len )
{
    //拼接请求头
    add_content_length( content_len );
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length( int content_len )
{
    return add_response( "Content-Length: %d\r\n", content_len );
}

//根据m_linger开启长连接
bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

//添加结尾空行
bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

//添加内容
bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

//向客户端（浏览器）写回相应报文
bool http_conn::process_write( HTTP_CODE ret )
{
    switch ( ret )
    {
        case INTERNAL_ERROR:
        {
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) )
            {
                return false;
            }
            break;
        }
        case BAD_REQUEST:
        {
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) )
            {
                return false;
            }
            break;
        }
        case NO_RESOURCE:
        {
            stat( m_real_file, &m_file_stat );
            strcpy( m_real_file, doc_root );
            int len = strlen( doc_root );
            char *m_real_url = (char *)malloc(sizeof(char) * 200);
            strcpy(m_real_url, "/404.html");
            strncpy(m_real_file + len, m_real_url, strlen(m_real_url));
            free(m_real_url);

            add_status_line( 404, error_404_title );
            if( m_file_stat.st_size != 0 ){
                add_headers( strlen( error_404_form ) );
                m_iv[ 0 ].iov_base = m_write_buf;
                m_iv[ 0 ].iov_len = m_write_idx;
                m_iv[ 1 ].iov_base = m_file_address;
                m_iv[ 1 ].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            }
            return false;
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            add_status_line( 403, error_403_title );
            add_headers( strlen( error_403_form ) );
            if ( ! add_content( error_403_form ) )
            {
                return false;
            }
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line( 200, ok_200_title );
            if ( m_file_stat.st_size != 0 )
            {
                add_headers( m_file_stat.st_size );
                m_iv[ 0 ].iov_base = m_write_buf;
                m_iv[ 0 ].iov_len = m_write_idx;
                m_iv[ 1 ].iov_base = m_file_address;
                m_iv[ 1 ].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            }
            else
            {
                const char* ok_string = "<html><body></body></html>";
                add_headers( strlen( ok_string ) );
                if ( ! add_content( ok_string ) )
                {
                    return false;
                }
            }
        }
        case DIR_REQUEST:
        {
            //添加响应行
            add_status_line( 200, ok_200_title );
            const char* ok_string = "<html><body></body></html>";
            add_headers( strlen( ok_string ) );
            if ( ! add_content( ok_string ) )
            {
                return false;
            }
            return true;
        }
        default:
        {
            return false;
        }
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

void http_conn::process()   //线程从工作队列中取到任务后就会执行这个函数
{
    HTTP_CODE read_ret = process_read();    //从读缓存区中，读取一个http协议，并进行解析
    if ( read_ret == NO_REQUEST )
    {
        modfd( m_epollfd, m_sockfd, EPOLLIN );
        return;
    }

    bool write_ret = process_write( read_ret ); //解析完后向写缓冲写回响应报文段，等可写事件发生写回数据
    if ( ! write_ret )
    {
        close_conn();
    }

    modfd( m_epollfd, m_sockfd, EPOLLOUT ); //将fd事件改为可写
}
