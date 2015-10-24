
#pragma once

extern "C" {
#include <unistd.h>
#include <stdint.h>
#include <curl/curl.h>
#include <openssl/md5.h>
}
#include <cctype>
#include <cstring>
#include <cstdio>
#include <exception>
#include <algorithm>
#include <utility>
#include <vector>
#include <string>
#include <iostream>

// convention
#define BGY_PROTOCOL_VERSION_MAJOR   0        // 主版本号
#define BGY_PROTOCOL_VERSION_MINOR   1        // 分支版本号
#define BGY_PROTOCOL_VERSION_PATCH   0        // 补丁版本号
#define BGY_CONNECT_TIMEOUT          10              // curl连接超时时间（秒）
#define BGY_REQUEST_TIMEOUT                  60              // curl请求超时时间（秒）
#define BGY_USER_AGENT               "KaoQinJi"      // http头 User-Agent 值
#define BGY_PROTOCOL_VERSION_KEY     "protocol_version"      // 协议版本号参数 键名
#define BGY_SIGN_KEY                 "sign"     // 签名参数 键名
#define BGY_URL_MAX_LENGTH           4096       // URL 最大长度
#define BGY_SIGN_HYPHEN              "|"        // 签名字符片段连接符
#define BGY_RESPONSE_MAX_CONTENT_LENGTH     INT_MAX     // http响应中 Content-Length 最大值，超过此值请求不会被处理。
#define BGY_FREAD_BUFFER_SIZE        4096       // 读文件时 buffer 字节数（NOTE：栈上分配）


#define BGY_STRINGIZE_(var)          #var
#define BGY_STRINGIZE(var)           BGY_STRINGIZE_(var)
#define BGY_STRLITERAL_LEN(str)      (sizeof(str) - 1)

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

#define BGY_PROTOCOL_VERSION                                                            \
    BGY_STRINGIZE(BGY_PROTOCOL_VERSION_MAJOR) "."                                       \
    BGY_STRINGIZE(BGY_PROTOCOL_VERSION_MINOR) "."                                       \
    BGY_STRINGIZE(BGY_PROTOCOL_VERSION_PATCH)


namespace bgy {

enum HttpMethod { GET = 1, POST = 2, };
typedef std::pair<std::string, std::string> StringPair;
typedef std::pair<const std::string*, const std::string*> StringPtrPair;
typedef std::vector<StringPair> StringPairList;
typedef std::vector<std::string> StringList;


// 用于提供异常安全保证
template<typename PtrType, void (*freeResourceFunc) (PtrType)>
class SafePtr
{
public:
    SafePtr():
        src(NULL)
    {}

    explicit SafePtr(PtrType _src):
        src(_src)
    {}

    PtrType get() const
    {
        return src;
    }

    PtrType& getRef()
    {
        return src;
    }

    void reset(PtrType _src)
    {
        src = _src;
    }

    PtrType release()
    {
        PtrType ret = src;
        src = NULL;
        return ret;
    }

    // NOTE: RVO-only copyable. Be careful...
    SafePtr(const SafePtr& other):
        src(other.src)
    {}

    // RVO-only copyable
    SafePtr(SafePtr& other):
        src(other.release())
    {}

    // RVO-only copyable
    SafePtr& operator=(SafePtr& other)
    {
        reset(other.release());
        return *this;
    }

    ~SafePtr()
    {
        if (src != NULL)
        {
//            freeResourceFunc(src);
//            src = NULL;
        }
    }

private:
    PtrType src;
};

typedef SafePtr<CURL*, curl_easy_cleanup> SafeCurl;


typedef std::pair<const char*, size_t> RawStr;


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
        res.resize(length << 1);
        hex(data, length, res.begin(), res.end());
        return res;
    }

    template<typename It>
    static bool hex(const uint8_t* data, std::size_t length, It begin, const It end)
    {
        if (end - begin < (length << 1))
        {
            return false;
        }
        return hex(data, length, begin);
    }

    // NOTE: 外部保证 begin 足够长，否则溢出。
    template<typename It>
    static bool hex(const uint8_t* data, std::size_t length, It begin)
    {
        for (const uint8_t* dataEnd = data + length; data != dataEnd; ++data)
        {
            *begin++ = hex(*data >> 4);
            *begin++ = hex(*data & 0x0f);
        }
        return true;
    }

    static char hexUpper(uint8_t chr) __attribute__((const))
    {
        return chr + ((chr < 10) ? '0' : ('A' - 10));
    }

    static bool startsWith(const std::string& str, const char* prefix, std::size_t prefixLen)
    {
        return str.size() >= prefixLen && startsWith(str.c_str(), prefix, prefixLen);
    }

    static bool startsWith(const char* str, const char* prefix, std::size_t prefixLen)
    {
        return std::strncmp(str, prefix, prefixLen) == 0;
    }

    static std::string toLowerCase(const std::string& str)
    {
        std::string res;
        res.resize(str.size());
        std::transform(str.begin(), str.end(), res.begin(), tolower);
        return res;
    }

    static std::string toLowerCase(const char* str, size_t len)
    {
        std::string res;
        res.resize(len);
        std::transform(str, str + len, res.begin(), tolower);
        return res;
    }

    // urlencode(str) => res[0, n]; return res+n; 保证不会越过end，越界前返回 NULL.
    static char* urlEncode(const std::string& str, char* res, const char* const end)
    {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(str.data());
        const uint8_t* plainBegin = data;
        for (const uint8_t* dataEnd = data + str.length(); data != dataEnd && res < end; ++data)
        {
            if (!(std::isalnum(*data) ||
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
            if (!(std::isalnum(*data) ||
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

    static void freeFile(std::FILE* fp)
    {
        std::fclose(fp);
    }

    static void freeCharArray(char* str)
    {
        delete[] str;
    }

    static void paste(char*& dest, const void* src, std::size_t len)
    {
        std::memcpy(dest, src, len);
        dest += len;
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


// generic exception with an error code inside.
class Error:
    public std::exception
{
private:
    int32_t _code;
    const std::string message;
public:
    Error(int32_t __code, const std::string& _message):
        exception(), _code(__code), message(_message)
    {}

    virtual const char* what() const _GLIBCXX_USE_NOEXCEPT
    {
        return message.c_str();
    }

    const int32_t code() const
    {
        return _code;
    }

    virtual ~Error() _GLIBCXX_USE_NOEXCEPT {}
};


class CurlScope
{
public:
    CurlScope()
    {
        init();
    }

    ~CurlScope()
    {
        destroy();
    }

private:
    static void init()
    {
        static int done = 0;
        static bool check = false;
        if (__sync_fetch_and_add(&done, 1) == 0)
        {
            CURLcode code = curl_global_init(CURL_GLOBAL_ALL);
            if (code == CURLE_OK)
            {
                check = true;
            }
            else
            {
                throw Error(code, curl_easy_strerror(code));
            }
        }
        else
        {
            __sync_sub_and_fetch(&done, 1);
            while (!check)
            {
                usleep(20);
            }
        }
    }

    static void destroy()
    {
        static int done = 0;
        static bool check = false;
        if (__sync_fetch_and_add(&done, 1) == 0)
        {
            curl_global_cleanup();
            check = true;
        }
        else
        {
            __sync_sub_and_fetch(&done, 1);
            while (!check)
            {
                usleep(20);
            }
        }
    }
};


class MD5Stream
{
    friend MD5Stream& operator>>(MD5Stream&, char*);
    friend MD5Stream& operator>>(MD5Stream&, std::string&);
private:
    MD5_CTX ctx;
    bool finished;
    bool ok;

public:
    enum { RESULT_SIZE = MD5_DIGEST_LENGTH << 1 };

    MD5Stream():
        finished(false), ok(true)
    {
        MD5_Init(&ctx);
    }

    const std::string hex()
    {
        if (finished) { return std::string(); }
        uint8_t binary[MD5_DIGEST_LENGTH];
        MD5_Final(binary, &ctx);
        finished = true;
        return Aside::hex(binary, sizeof(binary));
    }

    template<typename It>
    const bool hex(It begin, const It end)
    {
        if (finished) { return false; }
        uint8_t binary[MD5_DIGEST_LENGTH];
        MD5_Final(binary, &ctx);
        finished = true;
        return Aside::hex(binary, sizeof(binary), begin, end);
    }

    // NOTE: 外部保证 begin 足够长，否则溢出。
    template<typename It>
    const bool hex(It begin)
    {
        if (finished) { return false; }
        uint8_t binary[MD5_DIGEST_LENGTH];
        MD5_Final(binary, &ctx);
        finished = true;
        return Aside::hex(binary, sizeof(binary), begin);
    }

    MD5Stream& append(const char* data, std::size_t len)
    {
        if (finished) { ok = false; }
        if (len)
        {
            MD5_Update(&ctx, data, len);
        }
        return *this;
    }

    bool good() const
    {
        return ok;
    }

    static std::string md5File(const char* file)
    {
        std::string res;
        SafePtr<std::FILE*, &Aside::freeFile> fp(std::fopen(file, "rb"));
        if (fp.get())
        {
            MD5Stream stream;
            while (!feof(fp.get()))
            {
                char buffer[BGY_FREAD_BUFFER_SIZE];
                std::size_t read = std::fread(buffer, 1, sizeof(buffer), fp.get());
                stream.append(buffer, read);
            }
            stream >> res;
        }
        return res;
    }

    ~MD5Stream()
    {
        if (!finished)
        {
            uint8_t binary[MD5_DIGEST_LENGTH];
            MD5_Final(binary, &ctx);
        }
    }
};

MD5Stream& operator<<(MD5Stream& stream, const std::string& data)
{
    stream.append(data.data(), data.size());
    return stream;
}

MD5Stream& operator<<(MD5Stream& stream, const char* data)
{
    stream.append(data, std::strlen(data));
    return stream;
}

MD5Stream& operator<<(MD5Stream& stream, const RawStr& str)
{
    stream.append(str.first, str.second);
    return stream;
}

MD5Stream& operator<<(MD5Stream& stream, const char data)
{
    stream.append(&data, sizeof(char));
    return stream;
}

MD5Stream& operator>>(MD5Stream& stream, std::string& str)
{
    if (str.empty())
    {
        str.resize(MD5Stream::RESULT_SIZE);
    }
    stream.ok = stream.hex(str.begin(), str.end());
    return stream;
}

// NOTE: 外部保证 begin 足够长，否则溢出。
MD5Stream& operator>>(MD5Stream& stream, char* str)
{
    stream.ok = stream.hex(str);
    return stream;
}


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
