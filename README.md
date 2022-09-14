# httpServer

## 简介
这是一款Linux下C++轻量级Web服务器

## 特性
* 使用 **线程池 + 非阻塞socket + epoll + 模拟Proactor** 的并发处理模型
* 使用**状态机**解析HTTP请求报文，支持解析**GET和POST**请求
* 实现WEB端用户**注册、登录**功能，可以请求服务器**图片**
* 使用**c++11实现数据库连接池**连接数据库

## 快速运行
* 服务器测试环境
    * Unbuntu版本 18.04
    * MySQL版本 8.0.30
* 首先安装MySQL数据库
* 创建MySQL数据库并创建**user**表
``` mysql
//创建yourdb库
create database yourdb;

//创建user表
use yourdb;
CREATE TABLE user(
    id int not null primary key auto_increment,
    username char(100) not null unique,
    passwd char(100) not null 
)Engine=Innodb;

//添加数据
INSERT INTO user(username, passwd) VALUES('name', 'passwd');
```

* 修改 /mysqlpool/config.json
``` json
{
    "ip" : "localhost",
    "port" : 3306,
    "user" : "root",    //你的数据库登录名
    "password" : "passwd",  //你的数据库密码
    "db" : "yourdb",    //你刚刚创建的数据库表
    "minUser" : 5,
    "maxUser" : 80,
    "timeout" : 1000,   //1000毫秒，空闲了1000毫秒就释放
    "maxFreeTime" : 1000    //1000毫秒，当数据库连接池没有连接了，最多等待1000毫秒
}
```
* make
```
make
```
* 启动server
```
//指定10000端口
./server -10000
```

*浏览器端
```
ip:10000
```