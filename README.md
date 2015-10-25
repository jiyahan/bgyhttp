# bgy SDK
简单、非全功能的 http 客户端库。
特性：
* header only
* 线程安全
* 异常安全
* 兼容 posix 和 windows (windows 没测试）。

# requirements
* libcurl
* openssl (libcrypto)

# usage
将 bgy 目录的上层目录加入编译器包含目录即可。
外部只能使用 bgy::Client/Request/Response 三个类，不应该使用其余任何东西。
除跨线程使用同一个 bgy::Request 对象外，其余情况均线程安全。

# demo usage
posix:
$ make
$ ./request "http://119.254.1.6/fkwebserver/"
windows:
手工编译执行
