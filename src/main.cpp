
/**
 * g++ -Wall -g3 -O0 main.cpp -o request -lcurl -lcrypto
 * usage:
 * ./a.out "http://119.254.1.6/fkwebserver/?name=value&name2=value2"
 */

#include <iostream>
#include <cstdlib>
#include "Client.hpp"

int main(int argc, char* argv[])
{
    if (argc < 2 || argv[1] == std::string("-h") || argv[1] == std::string("--help"))
    {
        std::cerr << "usage: " << argv[0] << " <url>" << std::endl;
        std::exit(EXIT_SUCCESS);
    }

    bgy::StringPairList params;
    params.push_back(bgy::StringPair("aaa", "bbb"));

    bgy::CurlScope bgyCurlScope;    // 保证这个变量不被析构，直到所有 CURL 操作全部做完。
    {
        bgy::Client client;
        bgy::Request request(argv[1]);
        bgy::Response response(client.request(request));
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

    {
        bgy::Client client;
        bgy::Response response(client.get(argv[1], params));
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
