
#pragma once

extern "C" {
#include <curl/curl.h>
}
#include <string>
#include <vector>
#include "Aside.hpp"


namespace http {

class Response
{
public:
    Response():
        code(0)
    {}

    bool ok() const
    {
        return code == 200 || code == 204 || code == 301 || code == 302 || code == 304;
    }

    SafeCurl curl;  // 如果要使用 CURL* curl，必须设置 <Request>.noClean = true; 否则这里始终是 NULL。

    int16_t code;
    std::string contentType;
    std::string content;    // NOTE: 下载文件等情况 中间可能包含 '\0' 字符。
};

}
