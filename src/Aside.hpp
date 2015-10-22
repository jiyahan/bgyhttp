
#pragma once

extern "C" {
#include <stdint.h>
#include <curl/curl.h>
}
#include <exception>
#include <algorithm>
#include <vector>
#include <string>
#include <iostream>


#define __HTTP_PROTOCOL_VERSION_MAJOR  0        // 主版本号
#define __HTTP_PROTOCOL_VERSION_MINOR  1        // 分支版本号
#define __HTTP_PROTOCOL_VERSION_PATCH  0        // 补丁版本号
#define __HTTP_CONNECT_TIMEOUT  10              // curl连接超时时间（秒）
#define __HTTP_TIMEOUT          60              // curl请求超时时间（秒）
#define __HTTP_USER_AGENT       "KaoQinJi"      // http头 User-Agent 值
#define __HTTP_PROTOCOL_VERSION_KEY     "protocol_version"      // 协议版本号参数 键名
#define __HTTP_SIGN_KEY         "sign"          // 签名参数 键名


#define __HTTP_STRINGIZE_(var)   #var
#define __HTTP_STRINGIZE(var)   __HTTP_STRINGIZE_(var)

#ifdef NDEBUG
#   define __HTTP_ERR(...)      ::std::cerr << __FILE__ << ":"                      \
        << __LINE__ << ":" << __FUNCTION__ << "()\t"  << __VA_ARGS__ << std::endl;
#   define __HTTP_SAY(...)
#   define __HTTP_DUMP(...)
#else
#   ifdef __linux__
#       define __HTTP_ERR(...)      ::std::cerr << __FILE__ << ":"                  \
            << __LINE__ << ":" << __FUNCTION__ << "()\tERROR: [\033[32;31;5m"       \
            << __VA_ARGS__ << "\033[0m]" << std::endl;
#       define __HTTP_SAY(...)      ::std::cout << __FILE__ << ":"                  \
            << __LINE__ << ":" << __FUNCTION__ << "()"                              \
            << "\t[\033[32;49;5m" << __VA_ARGS__ << "\033[0m]" << std::endl;
#       define __HTTP_DUMP(...)      ::std::cout << __FILE__ << ":"                  \
            << __LINE__ << ":" << __FUNCTION__ << "()"                               \
            << "\t\033[32;34;5m" << #__VA_ARGS__ << "\033[0m: "                      \
            << "[\033[32;49;5m" << __VA_ARGS__ << "\033[0m]" << std::endl;
#   else
#       define __HTTP_ERR(...)      ::std::cerr << __FILE__ << ":"                      \
            << __LINE__ << ":" << __FUNCTION__ << "()\t"  << __VA_ARGS__ << std::endl;
#       define __HTTP_SAY(...)      ::std::cout << __FILE__ << ":"                  \
            << __LINE__ << ":" << __FUNCTION__ << "()"                              \
            << "\t[" << __VA_ARGS__ << "]" << std::endl;
#       define __HTTP_DUMP(...)      ::std::cout << __FILE__ << ":"                 \
            << __LINE__ << ":" << __FUNCTION__ << "()"                              \
            << "\t" << #__VA_ARGS__ << ": [" << __VA_ARGS__ << "]" << std::endl;
#   endif
#endif

#define __HTTP_PROTOCOL_VERSION                                                     \
    __HTTP_STRINGIZE(__HTTP_PROTOCOL_VERSION_MAJOR) "."                             \
    __HTTP_STRINGIZE(__HTTP_PROTOCOL_VERSION_MINOR) "."                             \
    __HTTP_STRINGIZE(__HTTP_PROTOCOL_VERSION_PATCH)


namespace http {

enum HttpMethod { GET = 1, POST = 2, };
typedef std::pair<std::string, std::string> StringPair;
typedef std::vector<StringPair> StringPairList;
typedef std::vector<std::string> StringList;


class Error:
    public std::exception
{
private:
    int32_t _code;
    const std::string& message;
public:
    Error(int32_t __code, const std::string& _message):
        exception(), _code(__code), message(_message)
    {}

    const char* what() const _GLIBCXX_USE_NOEXCEPT
    {
        return message.c_str();
    }

    const int32_t code() const
    {
        return _code;
    }
};


class Aside
{
public:
    static std::string hex(const uint8_t* data, std::size_t length)
    {
        std::string res;
        res.reserve(length << 1);
        for (const uint8_t* end = data + length; data != end; ++data)
        {
            res += hex(*data >> 4);
            res += hex(*data & 0x0f);
        }
        return res;
    }

    static char hex(uint8_t c) __attribute__((const))
    {
        return c < 10 ? ('0' + c) : ('a' + (c - 10));
    }

    // 将 curl_easy_setopt() 第三个参数转可视化，不能转的返回特定字符串
    static const std::string& toVisible(const std::string& val)
    {
        return val;
    }

    static const char* toVisible(const char* val)
    {
        return val;
    }

    static int toVisible(int val)
    {
        return val;
    }

    template<typename T>
    static const std::string toVisible(const T& val)
    {
        return std::string("<non-printable>");
    }
};

class StringPairCmper
{
    std::less<std::string> cmper;
public:
    bool operator()(const StringPair& left, const StringPair& right) const
    {
        return cmper(left.first, right.first);
    }
};

// 用于提供异常安全保证
template<typename Resource, void (*freeResource) (Resource)>
class SafeResource
{
public:
    typedef Resource ResourceType;

    SafeResource():
        src(NULL)
    {}

    explicit SafeResource(ResourceType _src):
        src(_src)
    {}

    Resource get() const
    {
        return src;
    }

    void reset(Resource _src)
    {
        src = _src;
    }

    Resource release()
    {
        Resource ret = src;
        src = NULL;
        return ret;
    }

    // NOTE: RVO-only copyable. Be careful...
    SafeResource(const SafeResource& other):
        src(other.src)
    {}

    // RVO-only copyable
    SafeResource(SafeResource& other):
        src(other.release())
    {}

    // RVO-only copyable
    SafeResource& operator=(SafeResource& other)
    {
        reset(other.release());
        return *this;
    }

    ~SafeResource()
    {
        __HTTP_DUMP((uint64_t)src);
        if (src != NULL)
        {
            freeResource(src);
            src = NULL;
        }
    }

private:
    ResourceType src;
};

typedef SafeResource<CURL*, curl_easy_cleanup> SafeCurl;


namespace {
// 确保 sizoef(char) == sizeof(std::string::value_type) == 1
template<int Size> class HttpCheckSize;
template<> class HttpCheckSize<1> { public: typedef int check_type; };
typedef HttpCheckSize<sizeof(char)>::check_type HttpCheckSizeTypedef;
typedef HttpCheckSize<sizeof(std::string::value_type)>::check_type HttpCheckSizeTypedef;
// 确保 unsigned char 与 uint8_t 是同一类型
template<typename T> class HttpCheckType;
template<> class HttpCheckType<unsigned char> { public: typedef int check_type; };
typedef HttpCheckType<uint8_t>::check_type HttpCheckTypeTypedef;
}

}
