
/**
 * usage:
 * ./a.out "http://119.254.1.6/fkwebserver/?name=value&name2=value2"
 */

#include <iostream>
#include <cstdlib>
#include "Client.hpp"

int main(int argc, char* argv[])
{
    __HTTP_DUMP(argc);
    if (argc < 2 || argv[1] == std::string("-h") || argv[1] == std::string("--help"))
    {
        std::cerr << "usage: " << std::endl;
        std::exit(EXIT_SUCCESS);
    }
    http::Client::init();
    http::Client client;
    http::Request request(argv[1]);
    http::Response response(client.request(request));
    __HTTP_DUMP(response.code);
    http::Client::destroy();
}
