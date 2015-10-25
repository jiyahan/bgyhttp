
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
        if (call != CURLE_OK)                                                               \
        {                                                                                   \
            BGY_ERR(BGY_STRINGIZE(call));                                                   \
            __VA_ARGS__;                                                                    \
        }                                                                                   \
    } while(false);
#else
#   define _BGY_CURL_CALL(call, ...)                                                        \
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


class UploadItem
{
public:
    const std::string* key;
    const char* basename;
    std::string md5;

    explicit UploadItem(const StringPair& item)
        : key(&item.first), basename(::basename(item.second.c_str())),
          md5(MD5Stream::md5File(item.second.c_str()))
    {}
};


class UploadItemCmper
{
public:
    bool operator()(const UploadItem& left, const UploadItem& right) const
    {
        int k = left.key->compare(*right.key);
        if (k)
        {
            return k < 0 ? false : true;
        }
        else
        {
            int b = std::strcmp(left.basename, right.basename);
            if (b)
            {
                return b < 0 ? false : true;
            }
            else
            {
                int m = left.md5.compare(right.md5);
                return m < 0 ? false : true;
            }
        }
    }
};


class Client
{
private:
    typedef SafePtr<struct curl_slist*, curl_slist_free_all> SafeCurlSlist;
    typedef std::pair<SafeCurl&, Response&> CurlResponsePair;
    typedef std::vector<StringPtrPair> StringPtrPairList;
    typedef SafePtr<char*, &Aside::freeCharArray> SafeCharArray;

    std::string secret;

    bool followLocation;            // 是否跟随重定向
    std::time_t connectTimeout;     // 连接超时(秒)
    std::time_t timeout;            // 请求超时(秒)

    std::string userAgent, protoVersionKey, protoVersion, signKey, signHyphen;

public:
    Client():
        secret(BGY_SECRET),
        followLocation(true),
        connectTimeout(BGY_CONNECT_TIMEOUT),
        timeout(BGY_REQUEST_TIMEOUT),
        userAgent(BGY_USER_AGENT),
        protoVersionKey(BGY_PROTOCOL_VERSION_KEY),
        protoVersion(BGY_PROTOCOL_VERSION),
        signKey(BGY_SIGN_KEY), signHyphen(BGY_SIGN_HYPHEN)
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

    Response post(const std::string& url, const StringPairList& params, const StringPairList& uploads)
    {
        return request(Request(url, params, uploads));
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
        if (req.url.empty()) { return resp; }
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
        return true;
    }

    SafeCurlSlist prepareHeaders(const Request& req) const
    {
        SafeCurlSlist headers;
        headers.reset(curl_slist_append(headers.get(), "Connection: close"));
        return headers;
    }

    bool prepareUpload(SafeCurl& ch, const Request& req) const
    {
        typedef SafePtr<curl_httppost*, curl_formfree> SafePost;
        StringPtrPairList paramPtrs = genParams(req);
        SafePost post, last;

        MD5Stream pstream;
        for (StringPtrPairList::const_iterator it = paramPtrs.begin(),
            lastIt = paramPtrs.end() - 1; it != paramPtrs.end(); ++it)
        {
            if (curl_formadd(&post.getRef(), &last.getRef(),
                CURLFORM_PTRNAME, it->first->c_str(),
                CURLFORM_PTRCONTENTS, it->second->c_str(),
                CURLFORM_END) != CURL_FORMADD_OK)
            {
                BGY_ERR("failed no curl_formadd()");
                return false;
            }
            if (!req.noSign)
            {
                pstream << *it->first << '=' << *it->second;
                if (it != lastIt)
                {
                    pstream << '&';
                }
            }
        }
        std::string paramSign;
        if (!req.noSign)
        {
            pstream >> paramSign;
            BGY_DUMP(paramSign);
        }

        typedef std::vector<UploadItem> UploadItemList;
        UploadItemList uploadItems;
        uploadItems.reserve(req.uploads.size());
        for (StringPairList::const_iterator it = req.uploads.begin(); it != req.uploads.end(); ++it)
        {
            if (*it->second.rbegin() == '/') { return false; }

            uploadItems.push_back(UploadItem(*it));
            const UploadItem& item = *uploadItems.rbegin();
            if (item.md5.empty()) { return false; }

            BGY_DUMP(it->first.c_str());
            BGY_DUMP(it->second.c_str());
            if (curl_formadd(&post.getRef(), &last.getRef(),
                CURLFORM_PTRNAME, it->first.c_str(),
                CURLFORM_FILE, it->second.c_str(),
                CURLFORM_FILENAME, item.basename,
                CURLFORM_END) != CURL_FORMADD_OK)
            {
                BGY_ERR("failed no curl_formadd()");
                return false;
            }
        }

        if (!req.noSign)
        {
            std::sort(uploadItems.begin(), uploadItems.end(), UploadItemCmper());

            std::string uploadParamSign;
            {
                MD5Stream stream;
                for (UploadItemList::const_iterator it = uploadItems.begin(),
                    lastIt = uploadItems.end() - 1; it != uploadItems.end(); ++it)
                {
                    stream << *it->key << '=' << *it->basename;
                    if (it != lastIt)
                    {
                        stream << '&';
                    }
                }
                stream >> uploadParamSign;
            }

            std::string sign;
            {
                MD5Stream gather;
                gather << secret << signHyphen << paramSign << signHyphen << uploadParamSign;
                for (UploadItemList::const_iterator it = uploadItems.begin(); it != uploadItems.end(); ++it)
                {
                    gather << signHyphen << it->md5;
                }
                gather >> sign;
            }

            if (curl_formadd(&post.getRef(), &last.getRef(),
                CURLFORM_PTRNAME, signKey.c_str(),
                CURLFORM_COPYCONTENTS, sign.c_str(),
                CURLFORM_END) != CURL_FORMADD_OK)
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
        if (!ok) { return false; }
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
        StringPtrPairList paramPtrs = genParams(req);
        SafeCharArray qs(new char[std::min<std::size_t>(
            calcEncodedMaxSize(paramPtrs, !req.noSign), BGY_URL_MAX_LENGTH)]);
        char* cursor = qs.get();
        const char* const end = cursor + BGY_URL_MAX_LENGTH;
        cursor += offset;

        MD5Stream stream;
        if (!req.noSign)
        {
            stream << secret << signHyphen;
        }
        for (StringPtrPairList::const_iterator it = paramPtrs.begin(),
            lastIt = paramPtrs.end() - 1; it != paramPtrs.end(); ++it)
        {
            cursor = Aside::urlEncode(*it->first, cursor, end);
            if (cursor == NULL || end - cursor < 2) { ok = false; return qs; }
            *cursor++ = '=';
            cursor = Aside::urlEncode(*it->second, cursor, end);
            if (!req.noSign || it != lastIt)
            {
                if (cursor == NULL) { ok = false; return qs; }
                *cursor++ = '&';
            }
            if (!req.noSign)
            {
                stream << *it->first << '=' << *it->second;
                if (it != lastIt)
                {
                    stream << '&';
                }
            }
        }

        if (!req.noSign)
        {
            if (cursor == NULL || end - cursor < (signKey.size() + 2 + MD5Stream::RESULT_SIZE))
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
        BGY_DUMP(size);
        BGY_DUMP(nmember);
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
            double contentLength = 0;
            _BGY_CURL_CALL(curl_easy_getinfo(ch.get(), CURLINFO_CONTENT_LENGTH_DOWNLOAD, &contentLength),
                resp.setProcessFailed(); return 0; );
            BGY_DUMP(contentLength);
            if (resp.contentLengthSpecified() && contentLength > BGY_RESPONSE_MAX_CONTENT_LENGTH)
            {
                BGY_ERR("bad Content-Length: " << contentLength);
                chp->canceled = true;
                return 0;   // 取消后续回调
            }
            resp.setStatusCode(statusCode);
            resp.setContentType(contentType);
            resp.setContentLength(contentLength);
        }
        if (resp.contentLengthSpecified() && static_cast<int64_t>(
            resp.content().size()) >= resp.contentLength()) { return 0; }

        const size_t length = size * nmember;
        resp.appendContent(static_cast<char*>(ptr), length);
        return length;
    }

    StringPtrPairList genParams(const Request& req) const
    {
        StringPtrPairList paramPtrs;
        for (StringPairList::const_iterator it = req.params.begin(); it != req.params.end(); ++it)
        {
            paramPtrs.push_back(std::make_pair(&it->first, &it->second));
        }
        paramPtrs.push_back(std::make_pair(&protoVersionKey, &protoVersion));
        std::sort(paramPtrs.begin(), paramPtrs.end(), StringPtrPairCmper());
        return paramPtrs;
    }

    bool signStr(const char* str, std::size_t len, char* outBegin) const
    {
        BGY_DUMP(std::string(str, len));
        MD5Stream stream;
        stream << secret << signHyphen << RawStr(str, len);
        stream >> outBegin;
        return stream.good();
    }

    std::size_t calcEncodedMaxSize(const StringPtrPairList& paramPtrs, bool sign) const
    {
        std::size_t size = paramPtrs.size() * 2;    // = & 个数 + 结尾的 0
        if (sign)
        {
            size += BGY_STRLITERAL_LEN(BGY_SIGN_KEY) * 3 + MD5Stream::RESULT_SIZE + 2;
        }
        for (StringPtrPairList::const_iterator it = paramPtrs.begin(); it != paramPtrs.end(); ++it)
        {
            size += (it->first->size() + it->second->size()) * 3;
        }
        return size;
    }
};

}

#undef _BGY_CURL_SETOPT
#undef _BGY_CURL_CALL
