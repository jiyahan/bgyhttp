
#pragma once

#include <utility>
#include <string>
#include <vector>
#include "Aside.hpp"

namespace http {

class Client;

class Request
{
    friend class Client;
public:
    explicit Request(const std::string& _url, HttpMethod _method = GET):
        noClean(false), url(_url), method(_method)
    {}

    Request(const std::string& _url, HttpMethod _method, const StringPairList& _params):
        noClean(false), url(_url), method(_method), params(_params)
    {}

    Request& addFile(const std::string& file)
    {
        uploads.push_back(file);
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

    virtual ~Request() {}

private:
    bool noClean;        // 请求结束后不做 curl_easy_cleanup(curl);

    std::string url;
    HttpMethod method;
    StringPairList headers, params;
    StringList uploads;

    std::string download;       // 下载文件保存路径

private:  // non-copyable
    Request(const Request&);
    Request& operator=(const Request&);
};

}
