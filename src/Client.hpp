
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
#   define __HTTP_CURL_CALL(call, ...)                                                      \
    do {                                                                                    \
        if (call != CURLE_OK)                                                               \
        {                                                                                   \
            __HTTP_ERR(__HTTP_STRINGIZE(call));                                             \
            __VA_ARGS__;                                                                    \
        }                                                                                   \
    } while(false);
#else
#   define __HTTP_CURL_CALL(call, ...)                                                      \
    do {                                                                                    \
        CURLcode code = call;                                                               \
        if (code != CURLE_OK)                                                               \
        {                                                                                   \
            __HTTP_ERR(__HTTP_STRINGIZE(call) << "=" << code                                \
                << ": " << curl_easy_strerror(code));                                       \
            __VA_ARGS__;                                                                    \
        }                                                                                   \
    } while (false);
#endif
#define __HTTP_CURL_SETOPT(ch, opt, param, ...)  __HTTP_CURL_CALL(curl_easy_setopt(ch, opt, param))


namespace http {

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
        connectTimeout(__HTTP_CONNECT_TIMEOUT),
        timeout(__HTTP_TIMEOUT),
        userAgent(__HTTP_USER_AGENT)
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
        __HTTP_DUMP((uint64_t)ch.get());
        return request(ch, req);
    }

    Response request(SafeCurl& ch, const Request& req) const
    {
        Response resp;
        if (send(ch, req))
        {
            __HTTP_SAY("send ok");
            int64_t statusCode, headerLength, contentLength;
            char* contentType = NULL;
            __HTTP_CURL_CALL(curl_easy_getinfo(ch.get(), CURLINFO_RESPONSE_CODE, &statusCode), return resp);
            __HTTP_CURL_CALL(curl_easy_getinfo(ch.get(), CURLINFO_HEADER_SIZE, &headerLength), return resp);
            __HTTP_CURL_CALL(curl_easy_getinfo(ch.get(), CURLINFO_CONTENT_TYPE, &contentType), return resp);
            __HTTP_CURL_CALL(curl_easy_getinfo(ch.get(), CURLINFO_CONTENT_LENGTH_DOWNLOAD, &contentLength), return resp);
            resp.code = statusCode;
            resp.contentType = contentType;
        }
        if (req.noClean)
        {
            resp.curl.reset(ch.release());
        }
        return resp;
    }

    bool send(SafeCurl& ch, const Request& req) const
    {
        // 特性设置
        __HTTP_CURL_SETOPT(ch.get(), CURLOPT_FOLLOWLOCATION, followLocation, return false);  // 跟随重定向
        __HTTP_CURL_SETOPT(ch.get(), CURLOPT_CONNECTTIMEOUT, connectTimeout, return false);  // 连接超时
        __HTTP_CURL_SETOPT(ch.get(), CURLOPT_TIMEOUT, timeout, return false);  // 请求超时
        // http头设置
        __HTTP_CURL_SETOPT(ch.get(), CURLOPT_USERAGENT, userAgent.c_str(), return false);
        SafeCurlSlist headers(prepareHeaders(req));
        __HTTP_CURL_SETOPT(ch.get(), CURLOPT_HTTPHEADER, headers.get(), return false);
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
            __HTTP_ERR("failed on prepare, http-method: " << req.method);
            return false;
        }

        __HTTP_CURL_CALL(curl_easy_perform(ch.get()), return false);
        return true;
    }

    size_t dataHandler(void* ptr, size_t size, size_t nmember, void* _resp)
    {
        Response* resp = static_cast<Response*>(_resp);
        resp->content.reserve(size * nmember);
        resp->content.append(reinterpret_cast<char*>(ptr), size);
        return size;
    }

    SafeCurlSlist prepareHeaders(const Request& req) const
    {
        SafeCurlSlist headers;
        __HTTP_DUMP((uint64_t)headers.get());
        headers.reset(curl_slist_append(headers.get(), "Connection: close"));
        return headers;
    }

    bool preparePost(SafeCurl& ch, const Request& req) const
    {
        __HTTP_CURL_SETOPT(ch.get(), CURLOPT_POST, 1, return false);
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
        std::string versionKey(__HTTP_PROTOCOL_VERSION_KEY), version(__HTTP_PROTOCOL_VERSION);
        paramPtrs.push_back(std::make_pair(&versionKey, &version));
        std::sort(paramPtrs.begin(), paramPtrs.end(), StringPtrPairCmper());

        SafeResource<char*, &Aside::freeRawStr> buffer(new char[__HTTP_URL_MAX_LENGTH]);
        char* cursor = buffer.get();
        char* end = cursor + __HTTP_URL_MAX_LENGTH;
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
        if (end - cursor < (__HTTP_STRLITERAL_LEN(__HTTP_SIGN_KEY) + 2 + signStr.size()))
        {
            return false;
        }
        Aside::paste(cursor, __HTTP_SIGN_KEY, __HTTP_STRLITERAL_LEN(__HTTP_SIGN_KEY));
        *cursor++ = '=';
        Aside::paste(cursor, signStr.data(), signStr.size());
        *cursor = '\0';
        __HTTP_CURL_SETOPT(ch.get(), CURLOPT_URL, buffer.get(), return false);
        __HTTP_DUMP(buffer.get());

//        std::string url;
//        std::size_t paramsOffset = req.url.size() + (req.url[req.url.size() - 1] != '?');
//        std::size_t paramsLength = 0;
//        {
//            for (StringPtrPairList::const_iterator it = paramPtrs.begin(); it != paramPtrs.end(); ++it)
//            {
//                paramsLength += it->first->size() + 1 + it->second->size();
//            }
//            paramsLength += (paramPtrs.size() - 1);
//            paramsLength -= (req.method != GET);
//            url.reserve(paramsOffset + paramsLength + 1
//                + sizeof(__HTTP_SIGN_KEY) + (MD5_DIGEST_LENGTH * 2));
//        }
//
//        url += req.url;
//        if ((req.url[req.url.size() - 1] != '?'))
//        {
//            url += '?';
//        }
//
//        for (StringPairList::const_iterator it = paramPtrs.begin(); it != paramPtrs.end(); ++it)
//        {
//            url += it->first;
//            url += '=';
//            url += it->second;
//            url += '&';
//        }
//
//        url += __HTTP_SIGN_KEY;
//        url += '=';
//        url += md5Str(url.data() + paramsOffset, paramsLength);
//        __HTTP_DUMP(url);
//
//        __HTTP_CURL_SETOPT(ch.get(), CURLOPT_URL, url.c_str(), return false);

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
        __HTTP_DUMP(std::string(str, len));
        uint8_t binary[MD5_DIGEST_LENGTH];
        MD5(reinterpret_cast<const unsigned char*>(str), len, binary);
        return Aside::hex(binary, sizeof(binary));
    }

    static void init()
    {
        static int done = 0;
        if (__sync_fetch_and_add(&done, 1) == 0)
        {
            __HTTP_DUMP(done);
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
            __HTTP_DUMP(done);
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

#undef __HTTP_CURL_SETOPT
