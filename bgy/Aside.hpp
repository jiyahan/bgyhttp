
#pragma once

#include "predef.hpp"
extern "C" {
#ifdef __unix__
#   include <unistd.h>
#elif defined(__WIN32__)
#   include <windows.h>
#endif
#include <stdint.h>
#include <openssl/md5.h>
#include <curl/curl.h>
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


namespace bgy {

enum HttpMethod { GET = 1, POST = 2, };
typedef std::pair<std::string, std::string> StringPair;
typedef std::pair<const std::string*, const std::string*> StrPtrPair;
typedef std::vector<StringPair> StrPairList;
typedef std::vector<std::string> StrList;


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

    PtrType& ref()
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
            freeResourceFunc(src);
            src = NULL;
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
        if (BGY_UNLIKELY(end - begin < static_cast<int64_t>(length << 1)))
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
                    if (BGY_UNLIKELY(end - res < data - plainBegin))
                    {
                        return NULL;
                    }
                    Aside::paste(res, plainBegin, data - plainBegin);
                }
                plainBegin = data + 1;
                if (BGY_UNLIKELY(end - res < 3))
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
                if (BGY_UNLIKELY(end - res < data - plainBegin))
                {
                    return NULL;
                }
                Aside::paste(res, plainBegin, data - plainBegin);
            }
        }
        else
        {
            if (BGY_UNLIKELY(end - res < static_cast<int64_t>(str.size())))
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


template<typename SecondType>
class StrPtrPairCmper
{
    std::less<std::string> cmper;
    typedef std::pair<const std::string*, SecondType> PairType;
public:
    bool operator()(const PairType& left, const PairType& right) const
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
#ifdef __unix__
                usleep(20);
#elif defined(__WIN32__)
                Sleep(1);
#endif
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
#ifdef __unix__
                usleep(20);
#elif defined(__WIN32__)
                Sleep(1);
#endif
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

inline MD5Stream& operator<<(MD5Stream& stream, const std::string& data)
{
    stream.append(data.data(), data.size());
    return stream;
}

inline MD5Stream& operator<<(MD5Stream& stream, const char* data)
{
    stream.append(data, std::strlen(data));
    return stream;
}

inline MD5Stream& operator<<(MD5Stream& stream, const RawStr& str)
{
    stream.append(str.first, str.second);
    return stream;
}

inline MD5Stream& operator<<(MD5Stream& stream, const char data)
{
    stream.append(&data, sizeof(char));
    return stream;
}

inline MD5Stream& operator>>(MD5Stream& stream, std::string& str)
{
    if (str.empty())
    {
        str.resize(MD5Stream::RESULT_SIZE);
    }
    stream.ok = stream.hex(str.begin(), str.end());
    return stream;
}

// NOTE: 外部保证 begin 足够长，否则溢出。
inline MD5Stream& operator>>(MD5Stream& stream, char* str)
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
