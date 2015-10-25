
/**
 * g++ -Wall -g3 -O0 main.cpp -o request -lcurl -lcrypto
 * usage:
 * ./a.out "http://119.254.1.6/fkwebserver/?name=value&name2=value2"
 */

#include <cstdlib>
#include <string>
#include <iostream>
#include "Client.hpp"

int main(int argc, char* argv[])
{
    if (argc < 2 || argv[1] == std::string("-h") || argv[1] == std::string("--help"))
    {
        std::cerr << "usage: " << argv[0] << " <url>" << std::endl;
        std::exit(EXIT_SUCCESS);
    }

    bgy::StrPairList params;
    params.push_back(bgy::StringPair("aaa", "bbb"));

    bgy::CurlScope bgyCurlScope;    // 保证这个变量不被析构，直到所有 CURL 操作全部做完。
    //*
    // demo:
    {
        bgy::Client client;
        bgy::Request request(argv[1]);
        request.addParam("name", "wumengchun")
            .addParam("bigname", "吴孟春")
            .addHeader("Cookie", "fdfdfdf=fdfdf");
        bgy::Response response(client.request(request));
        std::cout << std::string(40, '<') << std::endl;
        std::cout << response.content() << std::endl;
        std::cout << std::string(40, '>') << std::endl;
        BGY_DUMP(response.processSuccess());
        BGY_DUMP(response.ok());
        BGY_DUMP(response.statusCode());
        BGY_DUMP(response.contentType());
        BGY_DUMP(response.mimeType());
        BGY_DUMP(response.charset());
        BGY_DUMP(response.isHtml());
        BGY_DUMP(response.isJson());
        BGY_DUMP(response.isUtf8());
        BGY_DUMP(response.contentLength());
        BGY_DUMP(response.content().size());
    }
    std::cout << std::string(80, '-') << std::endl;

    // GET:
    {
        bgy::Client client;
        bgy::Response response(client.get(argv[1], params));
        std::cout << std::string(40, '<') << std::endl;
        std::cout << response.content() << std::endl;
        std::cout << std::string(40, '>') << std::endl;
        BGY_DUMP(response.processSuccess());
        BGY_DUMP(response.ok());
        BGY_DUMP(response.statusCode());
        BGY_DUMP(response.contentType());
        BGY_DUMP(response.mimeType());
        BGY_DUMP(response.charset());
        BGY_DUMP(response.isHtml());
        BGY_DUMP(response.isJson());
        BGY_DUMP(response.isUtf8());
        BGY_DUMP(response.content().size());
    }
    std::cout << std::string(80, '-') << std::endl;

    // POST:
    {
        bgy::Client client;
        bgy::Response response(client.post(argv[1], params));
        std::cout << std::string(40, '<') << std::endl;
        std::cout << response.content() << std::endl;
        std::cout << std::string(40, '>') << std::endl;
        BGY_DUMP(response.processSuccess());
        BGY_DUMP(response.ok());
        BGY_DUMP(response.statusCode());
        BGY_DUMP(response.contentType());
        BGY_DUMP(response.mimeType());
        BGY_DUMP(response.charset());
        BGY_DUMP(response.isHtml());
        BGY_DUMP(response.isJson());
        BGY_DUMP(response.isUtf8());
        BGY_DUMP(response.content().size());
    }
    std::cout << std::string(80, '-') << std::endl;
    // */

    // POST (with file upload):
    {
        bgy::StrPairList uploads;
        uploads.push_back(bgy::StringPair("filename10086", "/tmp/a.txt"));
        uploads.push_back(bgy::StringPair("filename17951", "/tmp/b.txt"));
        bgy::Client client;
        bgy::Response response = client.post(argv[1], params, uploads);
        std::cout << std::string(40, '<') << std::endl;
        std::cout << response.content() << std::endl;
        std::cout << std::string(40, '>') << std::endl;
        BGY_DUMP(response.processSuccess());
        BGY_DUMP(response.ok());
        BGY_DUMP(response.statusCode());
        BGY_DUMP(response.contentType());
        BGY_DUMP(response.mimeType());
        BGY_DUMP(response.charset());
        BGY_DUMP(response.isHtml());
        BGY_DUMP(response.isJson());
        BGY_DUMP(response.isUtf8());
        BGY_DUMP(response.content().size());
    }
}
