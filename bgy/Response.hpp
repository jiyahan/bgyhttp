
#pragma once

#include <cctype>
#include <cstring>
#include <string>
#include <vector>
#include "Aside.hpp"


#define _BGY_CONTENT_TYPE_JSON_1    "application/json"
#define _BGY_CONTENT_TYPE_JSON_2    "text/json"
#define _BGY_CONTENT_TYPE_HTML      "text/html"
#define _BGY_CONTENT_TYPE_CHARSET   "charset="


namespace bgy {

class Client;

class Response
{
    friend class Client;
public:
    Response():
        processFailed(false), _statusCode(0), _contentLength(0)
    {}

    // NOTE: Content-Type 没指定 charset 的情况下，这里返回 false。
    bool isUtf8() const
    {
        return isUtf8(charset());
    }

    bool charsetNotSpecified() const
    {
        return charset().empty();
    }

    bool isHtml() const
    {
        return Aside::startsWith(lowerContentType, _BGY_CONTENT_TYPE_HTML, BGY_STRLITERAL_LEN(_BGY_CONTENT_TYPE_HTML))
            && mimeTypeEndAt(BGY_STRLITERAL_LEN(_BGY_CONTENT_TYPE_HTML));
    }

    bool isJson() const
    {
        return (Aside::startsWith(lowerContentType, _BGY_CONTENT_TYPE_JSON_1, BGY_STRLITERAL_LEN(_BGY_CONTENT_TYPE_JSON_1))
                && mimeTypeEndAt(BGY_STRLITERAL_LEN(_BGY_CONTENT_TYPE_JSON_1)))
            || (Aside::startsWith(lowerContentType, _BGY_CONTENT_TYPE_JSON_2, BGY_STRLITERAL_LEN(_BGY_CONTENT_TYPE_JSON_2))
                && mimeTypeEndAt(BGY_STRLITERAL_LEN(_BGY_CONTENT_TYPE_JSON_2)));
    }

    bool ok() const
    {
        return processSuccess() && (_statusCode == 200 || _statusCode == 204
            || _statusCode == 301 || _statusCode == 302 || _statusCode == 304);
    }

    bool processSuccess() const
    {
        return !processFailed;
    }

    // HTTP Status Code.
    int16_t statusCode() const
    {
        return _statusCode;
    }

    // 返回 Content-Type 中的 MIME 部分，不包含结束的分号 。
    const std::string mimeType() const
    {
        std::size_t pos = lowerContentType.find(';', 1);
        if (pos != lowerContentType.npos)
        {
            return lowerContentType.substr(0, pos);
        }
        return std::string();
    }

    // NOTE: 返回全小写的 Content-Type，包含 charset=xx。
    const std::string& contentType() const
    {
        return lowerContentType;
    }

    // Content-Length: 值。 NOTE: 可能是 -1
    int64_t contentLength() const
    {
        return _contentLength;
    }

    bool contentLengthSpecified() const
    {
        return _contentLength >= 0;
    }

    const std::string& content() const
    {
        return _content;
    }

    // 从 Content-Type 中找出 charset 并转小写返回。数据异常情况下返回空字符串。
    std::string charset() const
    {
        if (lowerContentType.size() < 1024)
        {
            std::size_t pos = lowerContentType.find(_BGY_CONTENT_TYPE_CHARSET, 0, BGY_STRLITERAL_LEN(_BGY_CONTENT_TYPE_CHARSET));
            if (pos != lowerContentType.npos)
            {
                return lowerContentType.substr(pos + BGY_STRLITERAL_LEN(_BGY_CONTENT_TYPE_CHARSET));
            }
        }
        return std::string();
    }

    SafeCurl curl;  // 如果要使用 curl.src，必须设置 <Request>.noClean = true; 否则这里始终是 NULL。

private:
    void setContentType(const char* _contentType)
    {
        lowerContentType = _contentType == NULL ? std::string()
            : Aside::toLowerCase(_contentType, std::strlen(_contentType));
    }

    void setStatusCode(int16_t __statusCode)
    {
        _statusCode = __statusCode;
    }

    void setProcessFailed()
    {
        processFailed = true;
    }

    void setContentLength(std::size_t __contentLength)
    {
        _contentLength = __contentLength;
        if (_contentLength > 0)
        {
            _content.reserve(_contentLength);
        }
    }

    void appendContent(char* str, std::size_t length)
    {
        _content.append(str, length);
    }

private:
    bool processFailed;         // 是否处理出错
    int16_t _statusCode;        // HTTP Status Code
    std::string lowerContentType;   // 转小写后的 Content-Type: 值
    int64_t _contentLength; // Content-Length: 值。NOTE： 可能会是 -1
    std::string _content;       // NOTE: 下载文件等情况 中间可能包含 '\0' 字符。

    bool isUtf8(const std::string& _charset) const
    {
        return Aside::startsWith(_charset, "utf-8", BGY_STRLITERAL_LEN("utf-8"))
            && metaEndAt(_charset, BGY_STRLITERAL_LEN("utf-8"));
    }

    bool mimeTypeEndAt(std::size_t pos) const
    {
        return metaEndAt(lowerContentType, pos);
    }

    bool metaEndAt(const std::string& meta, std::size_t pos) const
    {
        return meta.size() <= pos || meta[pos] == ';' || std::isspace(meta[pos]);
    }
};

}
