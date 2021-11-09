# presscall

本工具使用多线程来模拟N个并发事务发送http或tcp请求，并统计时间，成功率等数据。用来做压力测试。
实现user_func.cpp CUserFunc::DoOnce接口来表达一次完整事务过程。

注意：连接未建立成功时只统计，不计算进成功率中。

20191005 chaoguodeng
1、重新设计多线程间免锁读写机制，实现无锁控制；
2、socket连接由非阻塞改为阻塞模式，提高并发效率，并发数可以突破1024；
3、优化内存，减少不必要的全局变量；
4、优化CPU，准确计算响应消息大小，减少不必要的循环；
5、美化打印格式；
6、增加信号机制，加快结束时退出速度；
7、支持https；
8、重构代码，可以很方便的扩展更多协议支持；
10、增加服务端证书验证和https双向认证；
11、支持设置本地发包地址；
12、支持每个线程使用不同的server端口；


编译时报错：致命错误：openssl/ssl.h：没有那个文件或目录
安装libssl-dev
apt-get install libssl-dev build-essential zlibczlib-bin libidn11-dev libidn11
yum -y install openssl-devel
或手工安装
或者重新安装openssl也可以解决

