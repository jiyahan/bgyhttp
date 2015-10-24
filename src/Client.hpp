
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
#   define _BGY_CURL_CALL(call, ...)                                                         \
    do {                                                                                    \
        if (call != CURLE_OK)                                                               \
        {                                                                                   \
            BGY_ERR(BGY_STRINGIZE(call));                                                   \
            __VA_ARGS__;                                                                    \
        }                                                                                   \
    } while(false);
#else
#   define _BGY_CURL_CALL(call, ...)                                                         \
    do {                                                                                    \
        CURLcode code = call;                                                               \
        if (code != CURLE_OK)                                                               \
        {                                                                                   \
            BGY_ERR(BGY_STRINGIZE(call) << "=" << code                                      \
                << ": " << curl_easy_strerror(code));                                       \
            __VA_ARGS__;                                                                    \
        }                                                                                   \
    } while (false);
#endif
#define _BGY_CURL_SETOPT(ch, opt, param, ...)  _BGY_CURL_CALL(curl_easy_setopt(ch, opt, param), __VA_ARGS__)


namespace bgy {

class Client
{
private:
    typedef SafePtr<struct curl_slist*, curl_slist_free_all> SafeCurlSlist;
    typedef std::pair<SafeCurl&, Response&> CurlResponsePair;

    std::string secret;

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
        return request(ch, req);
    }

    virtual ~Client() {}

private:
    class CurlHandlerParam
    {
    public:
        SafeCurl& ch;
        Response& response;
        bool headerProcessed, canceled;

        CurlHandlerParam(SafeCurl& _ch, Response& _response):
            ch(_ch), response(_response),
            headerProcessed(false), canceled(false)
        {}
    };

    Response request(SafeCurl& ch, const Request& req) const
    {
        Response resp;
        if (request(ch, req, resp))
        {
            if (resp.content().size() != resp.contentLength())
            {
                resp.setProcessFailed();
            }
        }
        else
        {
            resp.setProcessFailed();
        }
        if (req.noClean)
        {
            resp.curl.reset(ch.release());
        }
        return resp;
    }

    bool request(SafeCurl& ch, const Request& req, Response& resp) const
    {
        // 特性设置
        _BGY_CURL_SETOPT(ch.get(), CURLOPT_FOLLOWLOCATION, followLocation, return false);  // 跟随重定向
        _BGY_CURL_SETOPT(ch.get(), CURLOPT_CONNECTTIMEOUT, connectTimeout, return false);  // 连接超时
        _BGY_CURL_SETOPT(ch.get(), CURLOPT_TIMEOUT, timeout, return false);  // 请求超时
        // http头设置
        _BGY_CURL_SETOPT(ch.get(), CURLOPT_USERAGENT, userAgent.c_str(), return false);
        SafeCurlSlist headers(prepareHeaders(req));
        _BGY_CURL_SETOPT(ch.get(), CURLOPT_HTTPHEADER, headers.get(), return false);
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
        CurlHandlerParam chp(ch, resp);
        _BGY_CURL_SETOPT(ch.get(), CURLOPT_WRITEDATA, &chp);
        _BGY_CURL_SETOPT(ch.get(), CURLOPT_WRITEFUNCTION, &Client::contentHandler);

        _BGY_CURL_CALL(curl_easy_perform(ch.get()), return false);
        return true;
    }

    static size_t contentHandler(void* ptr, size_t size, size_t nmember, void* _chp)
    {
        CurlHandlerParam* chp = static_cast<CurlHandlerParam*>(_chp);
        if (chp->canceled) { return 0; }
        Response& resp = chp->response;

        if (!chp->headerProcessed)
        {
            chp->headerProcessed = true;
            SafeCurl& ch = chp->ch;
            int64_t statusCode;
            _BGY_CURL_CALL(curl_easy_getinfo(ch.get(), CURLINFO_RESPONSE_CODE, &statusCode),
                resp.setProcessFailed(); return 0;);
            char* contentType = NULL;
            _BGY_CURL_CALL(curl_easy_getinfo(ch.get(), CURLINFO_CONTENT_TYPE, &contentType),
                resp.setProcessFailed(); return 0; );
            double contentLength;
            _BGY_CURL_CALL(curl_easy_getinfo(ch.get(), CURLINFO_CONTENT_LENGTH_DOWNLOAD, &contentLength),
                resp.setProcessFailed(); return 0; );
            if (contentLength > BGY_RESPONSE_MAX_CONTENT_LENGTH)
            {
                chp->canceled = true;
                return 0;   // 停止后续回调
            }
            resp.setStatusCode(statusCode);
            resp.setContentType(contentType);
            resp.setContentLength(contentLength);
            resp._content.reserve(contentLength);
        }

        const size_t length = size * nmember;
        resp._content.append(static_cast<char*>(ptr), length);
        return length;
    }

    SafeCurlSlist prepareHeaders(const Request& req) const
    {
        SafeCurlSlist headers;
        headers.reset(curl_slist_append(headers.get(), "Connection: close"));
        return headers;
    }

    bool preparePost(SafeCurl& ch, const Request& req) const
    {
        _BGY_CURL_SETOPT(ch.get(), CURLOPT_POST, 1, return false);
        basename("");
        return true;
    }

    // TODO: 要求参数名值全部是标量，不能是数组==。
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

        SafePtr<char*, &Aside::freeCharArray> url(new char[BGY_URL_MAX_LENGTH]);
        char* cursor = url.get();
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
            if (!req.noSign || it != paramPtrs.end())
            {
                *cursor++ = '&';
            }
        }

        if (!req.noSign)
        {
            if (end - cursor < (BGY_STRLITERAL_LEN(BGY_SIGN_KEY) + 2 + MD5Stream::RESULT_SIZE))
            {
                return false;
            }
            const int paramLength = cursor - paramBegin - 1;
            Aside::paste(cursor, BGY_SIGN_KEY, BGY_STRLITERAL_LEN(BGY_SIGN_KEY));
            *cursor++ = '=';
            if (!signStr(paramBegin, paramLength, cursor))
            {
                return false;
            }
            cursor += MD5Stream::RESULT_SIZE;
            *cursor++ = '\0';
        }
        _BGY_CURL_SETOPT(ch.get(), CURLOPT_URL, url.get(), return false);
        BGY_DUMP(url.get());

        return true;
    }

    bool signStr(const char* str, std::size_t len, char* outBegin) const
    {
        BGY_DUMP(std::string(str, len));
        MD5Stream stream;
        stream << secret
            << RawStr(BGY_SIGN_SEPARATER, BGY_STRLITERAL_LEN(BGY_SIGN_SEPARATER))
            << RawStr(str, len);
        stream >> outBegin;
        return stream.good();
    }
};

}

#undef _BGY_CURL_SETOPT
#undef _BGY_CURL_CALL
