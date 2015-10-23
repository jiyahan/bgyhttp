
#pragma once

extern "C" {
#include <stdint.h>
#include <ctype.h>
#include <curl/curl.h>
}
#include <cstring>
#include <exception>
#include <algorithm>
#include <vector>
#include <string>
#include <iostream>

// convention
#define BGY_PROTOCOL_VERSION_MAJOR   0        // 主版本号
#define BGY_PROTOCOL_VERSION_MINOR   1        // 分支版本号
#define BGY_PROTOCOL_VERSION_PATCH   0        // 补丁版本号
#define BGY_CONNECT_TIMEOUT          10              // curl连接超时时间（秒）
#define BGY_TIMEOUT                  60              // curl请求超时时间（秒）
#define BGY_USER_AGENT               "KaoQinJi"      // http头 User-Agent 值
#define BGY_PROTOCOL_VERSION_KEY     "protocol_version"      // 协议版本号参数 键名
#define BGY_SIGN_KEY                 "sign"          // 签名参数 键名
#define BGY_URL_MAX_LENGTH           4096


#define BGY_STRINGIZE_(var)          #var
#define BGY_STRINGIZE(var)           BGY_STRINGIZE_(var)
#define BGY_STRLITERAL_LEN(str)      (sizeof(str) - 1)

#ifdef NDEBUG
#   define BGY_ERR(...)      ::std::cerr << __FILE__ << ":"                      \
        << __LINE__ << ":" << __FUNCTION__ << "()\t"  << __VA_ARGS__ << std::endl;
#   define BGY_SAY(...)
#   define BGY_DUMP(...)
#else
#   ifdef __linux__
#       define BGY_ERR(...)      ::std::cerr << __FILE__ << ":"                  \
            << __LINE__ << ":" << __FUNCTION__ << "()\tERROR: [\033[32;31;5m"       \
            << __VA_ARGS__ << "\033[0m]" << std::endl;
#       define BGY_SAY(...)      ::std::cout << __FILE__ << ":"                  \
            << __LINE__ << ":" << __FUNCTION__ << "()"                              \
            << "\t[\033[32;49;5m" << __VA_ARGS__ << "\033[0m]" << std::endl;
#       define BGY_DUMP(...)      ::std::cout << __FILE__ << ":"                  \
            << __LINE__ << ":" << __FUNCTION__ << "()"                               \
            << "\t\033[32;34;5m" << #__VA_ARGS__ << "\033[0m: "                      \
            << "[\033[32;49;5m" << __VA_ARGS__ << "\033[0m]" << std::endl;
#   else
#       define BGY_ERR(...)      ::std::cerr << __FILE__ << ":"                      \
            << __LINE__ << ":" << __FUNCTION__ << "()\t"  << __VA_ARGS__ << std::endl;
#       define BGY_SAY(...)      ::std::cout << __FILE__ << ":"                  \
            << __LINE__ << ":" << __FUNCTION__ << "()"                              \
            << "\t[" << __VA_ARGS__ << "]" << std::endl;
#       define BGY_DUMP(...)      ::std::cout << __FILE__ << ":"                 \
            << __LINE__ << ":" << __FUNCTION__ << "()"                              \
            << "\t" << #__VA_ARGS__ << ": [" << __VA_ARGS__ << "]" << std::endl;
#   endif
#endif

#define BGY_PROTOCOL_VERSION                                                     \
    BGY_STRINGIZE(BGY_PROTOCOL_VERSION_MAJOR) "."                             \
    BGY_STRINGIZE(BGY_PROTOCOL_VERSION_MINOR) "."                             \
    BGY_STRINGIZE(BGY_PROTOCOL_VERSION_PATCH)


namespace bgy {

enum HttpMethod { GET = 1, POST = 2, };
typedef std::pair<std::string, std::string> StringPair;
typedef std::pair<const std::string*, const std::string*> StringPtrPair;
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
    static char hex(uint8_t chr) __attribute__((const))
    {
        return chr + ((chr < 10) ? '0' : ('a' - 10));
    }

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

    static char hexUpper(uint8_t chr) __attribute__((const))
    {
        return chr + ((chr < 10) ? '0' : ('A' - 10));
    }

    static char* urlEncode(const std::string& str, char* res, char* end)
    {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(str.data());
        const uint8_t* plainBegin = data;
        for (const uint8_t* dataEnd = data + str.length(); data != dataEnd && res < end; ++data)
        {
            if (!(isalnum(*data) ||
                (*data == '-') ||
                (*data == '_') ||
                (*data == '.') ||
                (*data == '~')))
            {
                if (plainBegin < data)
                {
                    if (end - res < data - plainBegin)
                    {
                        return NULL;
                    }
                    Aside::paste(res, plainBegin, data - plainBegin);
                }
                plainBegin = data + 1;
                if (end - res < 3)
                {
                    return NULL;
                }
                *res++ = '%';
                *res++ = hexUpper(*data >> 4);
                *res++ = hexUpper(*data & 0x0f);
            }
        }
        if (plainBegin != reinterpret_cast<const uint8_t*>(str.data()))
        {
            if (plainBegin < data)
            {
                if (end - res < data - plainBegin)
                {
                    return NULL;
                }
                Aside::paste(res, plainBegin, data - plainBegin);
            }
        }
        else
        {
            if (end - res < str.size())
            {
                return NULL;
            }
            Aside::paste(res, str.data(), str.size());
        }
        return res;
    }

    static std::string urlEncode(const std::string& str)
    {
        std::string res;
        const uint8_t* data = reinterpret_cast<const uint8_t*>(str.data());
        const uint8_t* plainBegin = data;
        for (const uint8_t* end = data + str.length(); data != end; ++data)
        {
            if (!(isalnum(*data) ||
                (*data == '-') ||
                (*data == '_') ||
                (*data == '.') ||
                (*data == '~')))
            {
                if (plainBegin < data)
                {
                    res.append(plainBegin, data);
                }
                plainBegin = data + 1;
                res += '%';
                res += hexUpper(*data >> 4);
                res += hexUpper(*data & 0x0f);
            }
        }
        if (plainBegin != reinterpret_cast<const uint8_t*>(str.data()))
        {
            if (plainBegin < data)
            {
                res.append(plainBegin, data);
            }
        }
        else
        {
            res = str;
        }
        return res;
    }

    static void freeRawStr(char* str)
    {
        delete[] str;
    }

    static void paste(char*& dest, const void* src, std::size_t len)
    {
        std::memcpy(dest, src, len);
        dest += len;
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

class StringPtrPairCmper
{
    std::less<std::string> cmper;
public:
    bool operator()(const StringPtrPair& left, const StringPtrPair& right) const
    {
        return cmper(*left.first, *right.first);
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
        BGY_DUMP((uint64_t)src);
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
