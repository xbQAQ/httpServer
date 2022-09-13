server: http_conn.cpp http_conn.h locker.h main.cpp threadpool.h mysqlpool/mysqlpool.h mysqlpool/mysqlpool.cpp
	g++ http_conn.cpp http_conn.h locker.h main.cpp threadpool.h mysqlpool/mysqlpool.h mysqlpool/mysqlpool.cpp -o server -pthread -lmysqlclient -ljsoncpp -g

clean:
	rm -r server