
#pragma once

#include <utility>
#include <string>
#include <vector>
#include "Aside.hpp"

namespace bgy {

class Client;

class Request
{
    friend class Client;
public:
    explicit Request(const std::string& _url, HttpMethod _method = GET):
        noClean(false), url(_url), method(_method), noSign(false),
        queryStringBegan(url.find('?') != url.npos)
    {}

    Request(const std::string& _url, HttpMethod _method, const StringPairList& _params):
        noClean(false), url(_url), method(_method),
        params(_params), noSign(false),
        queryStringBegan(url.find('?') != url.npos)
    {}

    Request& addFile(const std::string& file)
    {
        uploads.push_back(file);
        if (method == GET)
        {
            setMethod(POST);
        }
        return *this;
    }

    Request& addHeader(const std::string& name, const std::string& value)
    {
        headers.push_back(std::make_pair(name, value));
        return *this;
    }

    Request& setMethod(HttpMethod _method)
    {
        method = _method;
        return *this;
    }

    // 请求结束后 是否 不释放CURL资源。true：不释放；false：释放。
    // 设置成不释放时 <Response>.curl 才会有效；否则 <Response>.curl == NULL。
    Request& setNoClean(bool _noClean = true)
    {
        noClean = _noClean;
        return *this;
    }

    // 设置 请求是否 不需要参数签名。true：不需要；false：需要。
    Request& setNoSign(bool _noSign = true)
    {
        noSign = _noSign;
        return *this;
    }

    virtual ~Request() {}

private:
    bool noClean;        // 请求结束后不做 curl_easy_cleanup(curl);

    std::string url;
    HttpMethod method;
    StringPairList headers, params;
    StringList uploads;
    bool noSign;

    std::string download;       // 下载文件保存路径

public:
    const bool queryStringBegan;

private:  // non-copyable
    Request(const Request&);
    Request& operator=(const Request&);
};

}
