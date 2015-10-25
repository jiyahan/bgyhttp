
#pragma once
extern "C" {
#include <unistd.h>
#include <openssl/md5.h>
#include <curl/curl.h>
}
#include <cassert>
#include <ctime>
#include <sstream>
#include "Aside.hpp"
#include "Request.hpp"
#include "Response.hpp"


#ifdef NDEBUG
#   define _BGY_CURL_CALL(call, ...)                                                        \
    do {                                                                                    \
        if (BGY_UNLIKELY(call != CURLE_OK))                                                               \
        {                                                                                   \
            BGY_ERR(BGY_STRINGIZE(call));                                                   \
            __VA_ARGS__;                                                                    \
        }                                                                                   \
    } while(false);
#else
#   define _BGY_CURL_CALL(call, ...)                                                        \
    do {                                                                                    \
        CURLcode code = call;                                                               \
        if (BGY_UNLIKELY(code != CURLE_OK))                                                               \
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
    typedef std::vector<StrPtrPair> StrPtrPairList;
    typedef SafePtr<char*, &Aside::freeCharArray> SafeCharArray;

    std::string secret;

    bool followLocation;            // 是否跟随重定向
    std::time_t connectTimeout;     // 连接超时(秒)
    std::time_t timeout;            // 请求超时(秒)

    std::string userAgent, protoVersionKey, protoVersion, signKey, signHyphen;

public:
    Client():
        secret(BGY_SECRET),
        followLocation(true),   // 是否跟随重定向
        connectTimeout(BGY_CONNECT_TIMEOUT),
        timeout(BGY_REQUEST_TIMEOUT),
        userAgent(BGY_USER_AGENT),
        protoVersionKey(BGY_PROTOCOL_VERSION_KEY),
        protoVersion(BGY_PROTOCOL_VERSION),
        signKey(BGY_SIGN_KEY), signHyphen(BGY_SIGN_HYPHEN)
    {}

    Response get(const std::string& url)
    {
        return request(url, GET, StrPairList());
    }

    Response get(const std::string& url, const StrPairList& params)
    {
        return request(url, GET, params);
    }

    Response post(const std::string& url)
    {
        return request(url, POST, StrPairList());
    }

    Response post(const std::string& url, const StrPairList& params)
    {
        return request(url, POST, params);
    }

    Response post(const std::string& url, const StrPairList& params, const StrPairList& uploads)
    {
        return request(Request(url, params, uploads));
    }

    Response request(const std::string& url, HttpMethod method, const StrPairList& params)
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
        if (req.url.empty()) { return resp; }
        if (request(ch, req, resp))
        {
            if (static_cast<int64_t>(resp.content().size()) != resp.contentLength() && resp.contentLengthSpecified())
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
        // 回调设置
        CurlHandlerParam chp(ch, resp);
        _BGY_CURL_SETOPT(ch.get(), CURLOPT_WRITEDATA, &chp);
        _BGY_CURL_SETOPT(ch.get(), CURLOPT_WRITEFUNCTION, &Client::contentHandler);
        // 拼参数
        bool prepareRes = false, perform = true;
        switch (req.method)
        {
        case GET:
            prepareRes = prepareGet(ch, req);
            break;
        case POST:
            if (req.uploads.empty())
            {
                prepareRes = preparePost(ch, req);
            }
            else
            {
                perform = false;
                prepareRes = prepareUpload(ch, req);
            }
            break;
        }
        if (!prepareRes)
        {
            BGY_ERR("failed on prepare request, http-method: " << req.method);
            return false;
        }
        if (perform)
        {
            _BGY_CURL_CALL(curl_easy_perform(ch.get()), return false);
        }
        if (!chp.headerProcessed)
        {
            processHeader(ch, resp);
        }
        return true;
    }

    SafeCurlSlist prepareHeaders(const Request& req) const
    {
        SafeCurlSlist headers;
        headers.reset(curl_slist_append(headers.get(), "Connection: close"));
        for (StrPairList::const_iterator it = req.headers.begin(); it != req.headers.end(); ++it)
        {
            headers.reset(curl_slist_append(headers.get(), (it->first + ": " + it->second).c_str()));
        }
        return headers;
    }

    bool prepareUpload(SafeCurl& ch, const Request& req) const
    {
        typedef SafePtr<struct curl_httppost*, curl_formfree> SafePost;
        StrPtrPairList paramPtrs = genPtrParams(req);
        SafePost post;
        struct curl_httppost* last = NULL;

        StrList signs;
        signs.reserve(!req.params.empty() + 1 + req.uploads.size());

        {
            MD5Stream stream;
            if (BGY_LIKELY(!req.noSign))
            {
                stream << secret << signHyphen << userAgent << signHyphen;
            }
            for (StrPtrPairList::const_iterator it = paramPtrs.begin(),
                lastIt = paramPtrs.end() - 1; it != paramPtrs.end(); ++it)
            {
                if (BGY_UNLIKELY(curl_formadd(&post.ref(), &last,
                    CURLFORM_PTRNAME, it->first->c_str(),
                    CURLFORM_PTRCONTENTS, it->second->c_str(),
                    CURLFORM_END) != CURL_FORMADD_OK))
                {
                    BGY_ERR("failed no curl_formadd()");
                    return false;
                }
                if (BGY_LIKELY(!req.noSign))
                {
                    stream << *it->first << '=' << *it->second;
                    if (it != lastIt)
                    {
                        stream << '&';
                    }
                }
            }
            if (BGY_LIKELY(!req.noSign))
            {
                std::string paramSign;
                stream >> paramSign;
                BGY_DUMP(paramSign);
                signs.push_back(paramSign);
            }
        }

        typedef std::pair<const std::string*, const char*> StrRawPair;
        typedef std::vector<StrRawPair> StrRawPairList;
        StrRawPairList fileKeyNames;
        fileKeyNames.reserve(req.uploads.size());
        for (StrPairList::const_iterator it = req.uploads.begin(); it != req.uploads.end(); ++it)
        {
            if (*it->second.rbegin() == '/') { return false; }
            fileKeyNames.push_back(StrRawPair(&it->first, ::basename(it->second.c_str())));
            {
                std::string fileSign = MD5Stream::md5File(it->second.c_str());
                if (fileSign.empty()) { return false; }
                signs.push_back(fileSign);
            }

            if (BGY_UNLIKELY(curl_formadd(&post.ref(), &last,
                CURLFORM_PTRNAME, it->first.c_str(),
                CURLFORM_FILE, it->second.c_str(),
                CURLFORM_FILENAME, fileKeyNames.rbegin()->second,
                CURLFORM_END) != CURL_FORMADD_OK))
            {
                BGY_ERR("failed no curl_formadd()");
                return false;
            }
        }

        if (BGY_LIKELY(!req.noSign))
        {
            std::sort(fileKeyNames.begin(), fileKeyNames.end(), StrPtrPairCmper<const char*>());

            {
                MD5Stream stream;
                stream << secret << signHyphen;
                for (StrRawPairList::const_iterator it = fileKeyNames.begin(),
                    lastIt = fileKeyNames.end() - 1; it != fileKeyNames.end(); ++it)
                {
                    stream << *it->first << '=' << it->second;
                    if (it != lastIt)
                    {
                        stream << '&';
                    }
                }
                std::string uploadParamSign;
                stream >> uploadParamSign;
                signs.push_back(uploadParamSign);
            }

            std::string sign;
            {
                std::sort(signs.begin(), signs.end(), std::less<std::string>());
                MD5Stream stream;
                stream << secret;
                for (StrList::const_iterator it = signs.begin(); it != signs.end(); ++it)
                {
                    stream << signHyphen << *it;
                }
                stream >> sign;
            }

            if (BGY_UNLIKELY(curl_formadd(&post.ref(), &last,
                CURLFORM_PTRNAME, signKey.c_str(),
                CURLFORM_COPYCONTENTS, sign.c_str(),
                CURLFORM_END) != CURL_FORMADD_OK))
            {
                BGY_ERR("failed no curl_formadd()");
                return false;
            }
        }

        _BGY_CURL_SETOPT(ch.get(), CURLOPT_URL, req.url.c_str(), return false);
        _BGY_CURL_SETOPT(ch.get(), CURLOPT_POST, 1, return false);
        _BGY_CURL_SETOPT(ch.get(), CURLOPT_HTTPPOST, post.get(), return false);
        _BGY_CURL_CALL(curl_easy_perform(ch.get()), return false);
        return true;
    }

    bool preparePost(SafeCurl& ch, const Request& req) const
    {
        bool ok = true;
        SafeCharArray url(fillParams(req, 0, ok));
        BGY_DUMP(url.get());
        if (BGY_UNLIKELY(!ok)) { return false; }
        _BGY_CURL_SETOPT(ch.get(), CURLOPT_URL, req.url.c_str(), return false);
        _BGY_CURL_SETOPT(ch.get(), CURLOPT_POST, 1, return false);
        _BGY_CURL_SETOPT(ch.get(), CURLOPT_POSTFIELDS, url.get(), return false);
        return true;
    }

    // TODO: 要求参数名值全部是标量，不能是数组==。
    bool prepareGet(SafeCurl& ch, const Request& req) const
    {
        char hyphen = req.queryStringBegan ? (*req.url.rbegin() == '&' ? 0 : '&') : '?';
        std::size_t padding = req.url.size() + !!hyphen;
        bool ok = true;
        SafeCharArray url(fillParams(req, padding, ok));
        if (!ok) { return false; }
        char* cursor = url.get();
        std::memcpy(cursor, req.url.c_str(), req.url.size());
        if (hyphen)
        {
            cursor[req.url.size()] = hyphen;
        }
        _BGY_CURL_SETOPT(ch.get(), CURLOPT_URL, url.get(), return false);
        BGY_DUMP(url.get());
        return true;
    }

    SafeCharArray fillParams(const Request& req, std::size_t offset, bool& ok) const
    {
        StrPtrPairList paramPtrs = genPtrParams(req);
        SafeCharArray qs(new char[std::min<std::size_t>(
            calcEncodedMaxSize(paramPtrs, !req.noSign), BGY_URL_MAX_LENGTH)]);
        char* cursor = qs.get();
        const char* const end = cursor + BGY_URL_MAX_LENGTH;
        cursor += offset;

        MD5Stream stream;
        if (BGY_LIKELY(!req.noSign))
        {
            stream << secret << signHyphen << userAgent << signHyphen;
        }
        for (StrPtrPairList::const_iterator it = paramPtrs.begin(),
            lastIt = paramPtrs.end() - 1; it != paramPtrs.end(); ++it)
        {
            cursor = Aside::urlEncode(*it->first, cursor, end);
            if (BGY_UNLIKELY(cursor == NULL || end - cursor < 2)) { ok = false; return qs; }
            *cursor++ = '=';
            cursor = Aside::urlEncode(*it->second, cursor, end);
            if (!req.noSign || it != lastIt)
            {
                if (BGY_UNLIKELY(cursor == NULL)) { ok = false; return qs; }
                *cursor++ = '&';
            }
            if (BGY_LIKELY(!req.noSign))
            {
                stream << *it->first << '=' << *it->second;
                if (it != lastIt)
                {
                    stream << '&';
                }
            }
        }

        if (BGY_LIKELY(!req.noSign))
        {
            if (cursor == NULL || end - cursor < static_cast<int64_t>(signKey.size() + 2 + MD5Stream::RESULT_SIZE))
            {
                ok = false;
                return qs;
            }
            Aside::paste(cursor, signKey.data(), signKey.size());
            *cursor++ = '=';
            stream >> cursor;
            cursor += MD5Stream::RESULT_SIZE;
            *cursor++ = '\0';
        }
        return qs;
    }

    static size_t contentHandler(void* ptr, size_t size, size_t nmember, void* _chp)
    {
        CurlHandlerParam* chp = static_cast<CurlHandlerParam*>(_chp);
        if (BGY_UNLIKELY(chp->canceled)) { return 0; }
        Response& resp = chp->response;

        if (!chp->headerProcessed)
        {
            chp->headerProcessed = true;
            if (!processHeader(chp->ch, resp))
            {
                chp->canceled = true;
                return 0;
            }
        }
        if (resp.contentLengthSpecified() && static_cast<int64_t>(
            resp.content().size()) >= resp.contentLength()) { return 0; }

        const size_t length = size * nmember;
        resp.appendContent(static_cast<char*>(ptr), length);
        return length;
    }

    StrPtrPairList genPtrParams(const Request& req) const
    {
        StrPtrPairList paramPtrs;
        for (StrPairList::const_iterator it = req.params.begin(); it != req.params.end(); ++it)
        {
            paramPtrs.push_back(std::make_pair(&it->first, &it->second));
        }
        paramPtrs.push_back(std::make_pair(&protoVersionKey, &protoVersion));
        std::sort(paramPtrs.begin(), paramPtrs.end(), StrPtrPairCmper<const std::string*>());
        return paramPtrs;
    }

    std::size_t calcEncodedMaxSize(const StrPtrPairList& paramPtrs, bool sign) const
    {
        std::size_t size = paramPtrs.size() * 2;    // = & 个数 + 结尾的 0
        if (sign)
        {
            size += signKey.size() * 3 + MD5Stream::RESULT_SIZE + 2;
        }
        for (StrPtrPairList::const_iterator it = paramPtrs.begin(); it != paramPtrs.end(); ++it)
        {
            size += (it->first->size() + it->second->size()) * 3;
        }
        return size;
    }

    static bool processHeader(SafeCurl& ch, Response& resp)
    {
        int64_t statusCode = 0;
        _BGY_CURL_CALL(curl_easy_getinfo(ch.get(), CURLINFO_RESPONSE_CODE, &statusCode),
            resp.setProcessFailed(); return false;);
        char* contentType = NULL;
        _BGY_CURL_CALL(curl_easy_getinfo(ch.get(), CURLINFO_CONTENT_TYPE, &contentType),
            resp.setProcessFailed(); return false;);
        double contentLength = 0;
        _BGY_CURL_CALL(curl_easy_getinfo(ch.get(), CURLINFO_CONTENT_LENGTH_DOWNLOAD, &contentLength),
            resp.setProcessFailed(); return false;);
        if (contentLength > BGY_RESPONSE_MAX_CONTENT_LENGTH && resp.contentLengthSpecified())
        {
            BGY_ERR("bad Content-Length: " << contentLength);
            return false;   // 取消后续回调
        }
        resp.setStatusCode(statusCode);
        resp.setContentType(contentType);
        resp.setContentLength(contentLength);
        return true;
    }
};

}

#undef _BGY_CURL_SETOPT
#undef _BGY_CURL_CALL
