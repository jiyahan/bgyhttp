
#pragma once

// 通信协议版本号:
#ifndef BGY_PROTOCOL_VERSION_MAJOR
#   define BGY_PROTOCOL_VERSION_MAJOR   0        // 主版本号
#endif
#ifndef BGY_PROTOCOL_VERSION_MINOR
#   define BGY_PROTOCOL_VERSION_MINOR   1        // 分支版本号
#endif
#ifndef BGY_PROTOCOL_VERSION_PATCH
#   define BGY_PROTOCOL_VERSION_PATCH   0        // 补丁版本号
#endif

#ifndef BGY_CONNECT_TIMEOUT
#   define BGY_CONNECT_TIMEOUT          10              // curl连接超时时间（秒）
#endif
#ifndef BGY_REQUEST_TIMEOUT
#   define BGY_REQUEST_TIMEOUT          60              // curl请求超时时间（秒）
#endif

#ifndef BGY_USER_AGENT
#   define BGY_USER_AGENT               "BGY-KaoQinJi"      // http头 User-Agent 值
#endif

#ifndef BGY_PROTOCOL_VERSION_KEY
#   define BGY_PROTOCOL_VERSION_KEY     "protocol_version"      // 协议版本号参数 键名
#endif

#ifndef BGY_SIGN_KEY
#   define BGY_SIGN_KEY                 "sign"     // 签名参数 键名
#endif

#ifndef BGY_URL_MAX_LENGTH
#   define BGY_URL_MAX_LENGTH           4096       // URL 最大长度
#endif

#ifndef BGY_SIGN_HYPHEN
#   define BGY_SIGN_HYPHEN              "|"        // 签名字符片段连接符
#endif

#ifndef BGY_RESPONSE_MAX_CONTENT_LENGTH
extern "C" {
#   include <limits.h>     // for INT_MAX
}
#   define BGY_RESPONSE_MAX_CONTENT_LENGTH     INT_MAX     // http响应中 Content-Length 最大值，超过此值请求不会被处理。
#endif

#ifndef BGY_FREAD_BUFFER_SIZE
#   define BGY_FREAD_BUFFER_SIZE        4096       // 读文件时 buffer 字节数（NOTE：栈上分配）
#endif


#define BGY_STRINGIZE_(var)          #var
#define BGY_STRINGIZE(var)           BGY_STRINGIZE_(var)
#define BGY_STRLITERAL_LEN(str)      (sizeof(str) - 1)

#ifndef BGY_PROTOCOL_VERSION
#   define BGY_PROTOCOL_VERSION                                                         \
        BGY_STRINGIZE(BGY_PROTOCOL_VERSION_MAJOR) "."                                   \
        BGY_STRINGIZE(BGY_PROTOCOL_VERSION_MINOR) "."                                   \
        BGY_STRINGIZE(BGY_PROTOCOL_VERSION_PATCH)
#endif

#ifdef NDEBUG
#   define BGY_ERR(...)      ::std::cerr << __FILE__ << ":"                             \
        << __LINE__ << ":" << __FUNCTION__ << "()\t"  << __VA_ARGS__ << std::endl;
#   define BGY_SAY(...)
#   define BGY_DUMP(...)
#else
#   if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#       define BGY_ERR(...)      ::std::cerr << __FILE__ << ":"                         \
            << __LINE__ << ":" << __FUNCTION__ << "()\tERROR: [\033[32;31;5m"           \
            << __VA_ARGS__ << "\033[0m]" << std::endl;
#       define BGY_SAY(...)      ::std::cout << __FILE__ << ":"                         \
            << __LINE__ << ":" << __FUNCTION__ << "()"                                  \
            << "\t[\033[32;49;5m" << __VA_ARGS__ << "\033[0m]" << std::endl;
#       define BGY_DUMP(...)      ::std::cout << __FILE__ << ":"                        \
            << __LINE__ << ":" << __FUNCTION__ << "()"                                  \
            << "\t\033[32;34;5m" << #__VA_ARGS__ << "\033[0m: "                         \
            << "[\033[32;49;5m" << __VA_ARGS__ << "\033[0m]" << std::endl;
#   else
#       define BGY_ERR(...)      ::std::cerr << __FILE__ << ":"                         \
            << __LINE__ << ":" << __FUNCTION__ << "()\t"  << __VA_ARGS__ << std::endl;
#       define BGY_SAY(...)      ::std::cout << __FILE__ << ":"                         \
            << __LINE__ << ":" << __FUNCTION__ << "()"                                  \
            << "\t[" << __VA_ARGS__ << "]" << std::endl;
#       define BGY_DUMP(...)      ::std::cout << __FILE__ << ":"                        \
            << __LINE__ << ":" << __FUNCTION__ << "()"                                  \
            << "\t" << #__VA_ARGS__ << ": [" << __VA_ARGS__ << "]" << std::endl;
#   endif
#endif

#ifdef __GNUC__
#   define BGY_LIKELY(cond)     __builtin_expect(cond, 1)
#   define BGY_UNLIKELY(cond)   __builtin_expect(cond, 0)
#else
#   define BGY_LIKELY(cond)     cond
#   define BGY_UNLIKELY(cond)   cond
#endif
