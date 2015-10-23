
#pragma once
extern "C" {
#include <curl/curl.h>
#include <openssl/md5.h>
}
#include <cassert>
#include <ctime>
#include <sstream>
#include "Aside.hpp"
#include "Request.hpp"
#include "Response.hpp"


#ifdef NDEBUG
#   define BGY_CURL_CALL(call, ...)                                                      \
    do {                                                                                    \
        if (call != CURLE_OK)                                                               \
        {                                                                                   \
            BGY_ERR(BGY_STRINGIZE(call));                                             \
            __VA_ARGS__;                                                                    \
        }                                                                                   \
    } while(false);
#else
#   define BGY_CURL_CALL(call, ...)                                                      \
    do {                                                                                    \
        CURLcode code = call;                                                               \
        if (code != CURLE_OK)                                                               \
        {                                                                                   \
            BGY_ERR(BGY_STRINGIZE(call) << "=" << code                                \
                << ": " << curl_easy_strerror(code));                                       \
            __VA_ARGS__;                                                                    \
        }                                                                                   \
    } while (false);
#endif
#define BGY_CURL_SETOPT(ch, opt, param, ...)  BGY_CURL_CALL(curl_easy_setopt(ch, opt, param), __VA_ARGS__)


namespace bgy {

class Client
{
private:
    typedef SafeResource<struct curl_slist*, curl_slist_free_all> SafeCurlSlist;

    bool followLocation;            // 是否跟随重定向
    std::time_t connectTimeout;     // 连接超时(秒)
    std::time_t timeout;            // 请求超时(秒)

    std::string userAgent;

public:
    Client():
        followLocation(true),
        connectTimeout(BGY_CONNECT_TIMEOUT),
        timeout(BGY_TIMEOUT),
        userAgent(BGY_USER_AGENT)
    {}

    Response get(const std::string& url)
    {
        return request(url, GET, StringPairList());
    }

    Response get(const std::string& url, const StringPairList& params)
    {
        return request(url, GET, params);
    }

    Response post(const std::string& url)
    {
        return request(url, POST, StringPairList());
    }

    Response post(const std::string& url, const StringPairList& params)
    {
        return request(url, POST, params);
    }

    Response request(const std::string& url, HttpMethod method, const StringPairList& params)
    {
        return request(Request(url, method, params));
    }

    Response request(const Request& req) const
    {
        SafeCurl ch(curl_easy_init());
        BGY_DUMP((uint64_t)ch.get());
        return request(ch, req);
    }

    Response request(SafeCurl& ch, const Request& req) const
    {
        Response resp;
        if (send(ch, req, resp))
        {
            BGY_SAY("send ok");
            int64_t statusCode, headerLength, contentLength;
            char* contentType = NULL;
            BGY_CURL_CALL(curl_easy_getinfo(ch.get(), CURLINFO_RESPONSE_CODE, &statusCode), return resp);
            BGY_CURL_CALL(curl_easy_getinfo(ch.get(), CURLINFO_HEADER_SIZE, &headerLength), return resp);
            BGY_CURL_CALL(curl_easy_getinfo(ch.get(), CURLINFO_CONTENT_TYPE, &contentType), return resp);
            BGY_CURL_CALL(curl_easy_getinfo(ch.get(), CURLINFO_CONTENT_LENGTH_DOWNLOAD, &contentLength), return resp);
            resp.code = statusCode;
            resp.contentType = contentType;
            BGY_DUMP(statusCode);
            BGY_DUMP(contentLength);
        }
        if (req.noClean)
        {
            resp.curl.reset(ch.release());
        }
        BGY_DUMP(resp.contentType);
        BGY_DUMP(resp.content.size());
        return resp;
    }

    bool send(SafeCurl& ch, const Request& req, Response& resp) const
    {
        // 特性设置
        BGY_CURL_SETOPT(ch.get(), CURLOPT_FOLLOWLOCATION, followLocation, return false);  // 跟随重定向
        BGY_CURL_SETOPT(ch.get(), CURLOPT_CONNECTTIMEOUT, connectTimeout, return false);  // 连接超时
        BGY_CURL_SETOPT(ch.get(), CURLOPT_TIMEOUT, timeout, return false);  // 请求超时
        // http头设置
        BGY_CURL_SETOPT(ch.get(), CURLOPT_USERAGENT, userAgent.c_str(), return false);
        SafeCurlSlist headers(prepareHeaders(req));
        BGY_CURL_SETOPT(ch.get(), CURLOPT_HTTPHEADER, headers.get(), return false);
        // 拼参数
        bool prepareRes = false;
        switch (req.method)
        {
        case GET:
            prepareRes = prepareGet(ch, req);
            break;
        case POST:
            prepareRes = preparePost(ch, req);
            break;
        }
        if (!prepareRes)
        {
            BGY_ERR("failed on prepare, http-method: " << req.method);
            return false;
        }
        // 回调设置
        BGY_CURL_SETOPT(ch.get(), CURLOPT_WRITEDATA, &resp);
        BGY_CURL_SETOPT(ch.get(), CURLOPT_WRITEFUNCTION, &Client::contentHandler);

        BGY_CURL_CALL(curl_easy_perform(ch.get()), return false);
        return true;
    }

    static size_t contentHandler(void* ptr, size_t size, size_t nmember, void* _resp)
    {
        BGY_DUMP(size);
        BGY_DUMP(nmember);
        const size_t length = size * nmember;
        Response* resp = static_cast<Response*>(_resp);
        resp->content.reserve(length);
        resp->content.append(reinterpret_cast<char*>(ptr), length);
        return length;
    }

    SafeCurlSlist prepareHeaders(const Request& req) const
    {
        SafeCurlSlist headers;
        BGY_DUMP((uint64_t)headers.get());
        headers.reset(curl_slist_append(headers.get(), "Connection: close"));
        return headers;
    }

    bool preparePost(SafeCurl& ch, const Request& req) const
    {
        BGY_CURL_SETOPT(ch.get(), CURLOPT_POST, 1, return false);
        return true;
    }

    // TODO: 要求参数全部是标量，不能是数组==。
    bool prepareGet(SafeCurl& ch, const Request& req) const
    {
        typedef std::vector<StringPtrPair> StringPtrPairList;
        StringPtrPairList paramPtrs;
        for (StringPairList::const_iterator it = req.params.begin(); it != req.params.end(); ++it)
        {
            paramPtrs.push_back(std::make_pair(&it->first, &it->second));
        }
        std::string versionKey(BGY_PROTOCOL_VERSION_KEY), version(BGY_PROTOCOL_VERSION);
        paramPtrs.push_back(std::make_pair(&versionKey, &version));
        std::sort(paramPtrs.begin(), paramPtrs.end(), StringPtrPairCmper());

        SafeResource<char*, &Aside::freeRawStr> buffer(new char[BGY_URL_MAX_LENGTH]);
        char* cursor = buffer.get();
        char* end = cursor + BGY_URL_MAX_LENGTH;
        std::memcpy(cursor, req.url.data(), req.url.size());
        cursor += req.url.size();
        *cursor++ = req.queryStringBegan ? '&' : '?';
        char* paramBegin = cursor;

        for (StringPtrPairList::const_iterator it = paramPtrs.begin(); it != paramPtrs.end(); ++it)
        {
            cursor = Aside::urlEncode(*it->first, cursor, end);
            if (cursor == NULL || end - cursor < 2)
            {
                return false;
            }
            *cursor++ = '=';
            cursor = Aside::urlEncode(*it->second, cursor, end);
            if (cursor == NULL || end - cursor < 1)
            {
                return false;
            }
            *cursor++ = '&';
        }
        std::string signStr = md5Str(paramBegin, cursor - paramBegin);
        if (end - cursor < (BGY_STRLITERAL_LEN(BGY_SIGN_KEY) + 2 + signStr.size()))
        {
            return false;
        }
        Aside::paste(cursor, BGY_SIGN_KEY, BGY_STRLITERAL_LEN(BGY_SIGN_KEY));
        *cursor++ = '=';
        Aside::paste(cursor, signStr.data(), signStr.size());
        *cursor = '\0';
        BGY_CURL_SETOPT(ch.get(), CURLOPT_URL, buffer.get(), return false);
        BGY_DUMP(buffer.get());

        return true;
    }

    std::string sign(const Request& req) const
    {
        MD5_CTX ctx;
        MD5_Init(&ctx);
//        MD5_Update(&ctx, str.data(), str.size());
        uint8_t digest[MD5_DIGEST_LENGTH];
        MD5_Final(digest, &ctx);
        return Aside::hex(digest, sizeof(digest));
    }

    std::string md5Str(const char* str, std::size_t len) const
    {
        BGY_DUMP(std::string(str, len));
        uint8_t binary[MD5_DIGEST_LENGTH];
        MD5(reinterpret_cast<const unsigned char*>(str), len, binary);
        return Aside::hex(binary, sizeof(binary));
    }

    static void init()
    {
        static int done = 0;
        if (__sync_fetch_and_add(&done, 1) == 0)
        {
            BGY_DUMP(done);
            CURLcode code = curl_global_init(CURL_GLOBAL_ALL);
            if (code != CURLE_OK)
            {
                throw Error(code, curl_easy_strerror(code));
            }
        }
        else
        {
            __sync_sub_and_fetch(&done, 1);
        }
    }

    static void destroy()
    {
        static int done = 0;
        if (__sync_fetch_and_add(&done, 1) == 0)
        {
            BGY_DUMP(done);
            curl_global_cleanup();
        }
        else
        {
            __sync_sub_and_fetch(&done, 1);
        }
    }

    virtual ~Client() {}
};

}

#undef BGY_CURL_SETOPT
