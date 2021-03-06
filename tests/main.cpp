
/**
 * debug 编译:
 * g++ -Wall -g3 -O0 main.cpp -o request -I.. -lcurl -lcrypto
 *
 * release 编译 （注意 -DNDEBUG，否则会打印一些 debug 信息）：
 * g++ -DNDEBUG -Wall -O2 main.cpp -o request -I.. -lcurl -lcrypto
 *
 * usage:
 * ./request "http://119.254.1.6/fkwebserver/"
 *
 * 密钥、协议版本号、User-Agent、连接超时/请求超时 等设置见 Aside.hpp 。
 */

#include "predef.hpp"       // tests/predef.hpp 为 bgy-http 设定了一些参数。
#ifdef __unix__
extern "C" {
#   include <unistd.h>
}
#endif
#include <cstdlib>
#include <cstdio>
#include <string>
#include <iostream>
#include <fstream>
#include "bgy/Client.hpp"   // 使用 bgy-http Client/Request/Response 只需要包含这一个头文件。


// 待上传的文件，posix下如果不存在会自动生成。
#define _FILE_TXT "/tmp/a.txt"
#define _FILE_IMG "/tmp/b.jpg"

// 签名用的密钥，与服务器端保持一致，每台设备都不一样。
#define BGY_SECRET                   "FJDFf*e^fegffdh&^gfbvoi&*jf|{{kdm(9"

int main(int argc, char* argv[])
{
    extern void prepare(int, char*[]);
    prepare(argc, argv);

    const std::string url(argv[1]);
    bgy::StrPairList params;    // 请求参数。
    params.push_back(bgy::StringPair("param-key", "param-value"));

    bgy::CurlScope bgyCurlScope;    // 保证这个变量不被析构，直到所有 CURL 操作全部做完。

    bgy::Client client;     // 线程安全。建议一次构造、重复利用。
    client.setSecret("FJDFf*e^fegffdh&^gfbvoi&*jf|{{kdm(9");    // 设置 签名用的密钥，与服务器端保持一致，每台设备都不一样。
    // bgy::Client client("FJDFf*e^fegffdh&^gfbvoi&*jf|{{kdm(9");    // 构造时传入 secret 也可以。

    //* 演示 Request/Response 的用法: 先构造<Request>；然后用<Client>.request(<Request>)发送，并接收<Response>。
    {
        bgy::Request request(url);
        request.addParam("name", "John Tom")    // 添加参数。（NOTE：参数值必须是标量，不支持数组、关联数组等）。
            .addParam("from", "大韩民国")
            .addHeader("DNT", "1")      // 添加HTTP头： DNT: 1
            .addHeader("Cookie", "cookieName=cookieVar;cookieKey=cookieValue")
            .addFile("key-1", _FILE_TXT)    // 添加上传文件（将会自动改用 POST）。
            .addFile("key-2", _FILE_IMG)    // 注意 key 不能重复。
            .setNoSign();   // 不生成参数签名（仅对服务器端不校验签名的接口设置）。
        bgy::Response response = client.request(request);

        if (response.ok())  // all ok
        {
            // Content-Type 里指定了 charset=utf-8 或者 没有指定任何 charset：
            if (response.isUtf8() || response.charsetNotSpecified())
            {
                if (response.isJson())  // Content-Type: application/json 或 text/json
                {
                    // process response.content() ...
                }
                else if (response.isHtml())// Content-Type: text/html
                {
                    // process response.content() ...
                }
                else
                {
                    if (response.mimeType() == "image/jpeg")
                    {
                        // process response.content() ...
                    }
                }
            }
        }

        std::cout << std::string(20, '-') << "↓↓ 响应正文↓↓ " << std::string(20, '-') << std::endl;
        std::cout << response.content() << std::endl;   // http响应正文。
        std::cout << std::string(20, '-') << "↑↑ 响应正文↑↑ " << std::string(20, '-') << std::endl;
        BGY_DUMP(response.processSuccess());    // 处理过程是否全部成功（处理成功 不代表结果正确）
        BGY_DUMP(response.ok());            // 是否一切OK：处理成功、结果正确。
        BGY_DUMP(response.statusCode());    // http状态码，比如 200,304,404等
        BGY_DUMP(response.contentType());   // http头中的 Content-Type 的小写形式，可能为空。比如 "text/html; charset=utf-8"
        BGY_DUMP(response.mimeType());      // http头中 Content-Type 中的 MIME 部分的小写形式。比如 "text/html"。
        BGY_DUMP(response.charset());       // http头中 Content-Type 中的 charset 部分的小写形式，可能为空。比如 "utf-8"。
        BGY_DUMP(response.isHtml());        // http头 Content-Type == "text/html"。
        BGY_DUMP(response.isJson());        // http头 Content-Type in ("application/json", "text/json")。
        BGY_DUMP(response.isUtf8());        // charset() == "utf-8"。
        BGY_DUMP(response.contentLength()); // http头中的 Content-Length 值，-1 表示没有设定。
        BGY_DUMP(response.content().size());    // 实际接收到的 http正文 长度。
    }
    std::cout << std::string(80, '-') << std::endl << std::endl << std::endl;   // */

    //* GET 便捷用法：
    {
        bgy::Response response = client.get(url, params);
        std::cout << std::string(20, '-') << "↓↓ 响应正文↓↓ " << std::string(20, '-') << std::endl;
        std::cout << response.content() << std::endl;
        std::cout << std::string(20, '-') << "↑↑ 响应正文↑↑ " << std::string(20, '-') << std::endl;
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
    std::cout << std::string(80, '-') << std::endl << std::endl << std::endl;   // */

    //* POST 便捷用法:
    {
        bgy::Response response = client.post(url, params);
        std::cout << std::string(20, '-') << "↓↓ 响应正文↓↓ " << std::string(20, '-') << std::endl;
        std::cout << response.content() << std::endl;
        std::cout << std::string(20, '-') << "↑↑ 响应正文↑↑ " << std::string(20, '-') << std::endl;
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
    std::cout << std::string(80, '-') << std::endl << std::endl << std::endl;   // */

    //* POST (with file upload) 便捷用法:
    {
        bgy::StrPairList uploads;
        uploads.push_back(bgy::StringPair("key-1", _FILE_TXT));
        uploads.push_back(bgy::StringPair("key-2", _FILE_IMG));

        bgy::Response response = client.post(url, params, uploads);
        std::cout << std::string(20, '-') << "↓↓ 响应正文↓↓ " << std::string(20, '-') << std::endl;
        std::cout << response.content() << std::endl;
        std::cout << std::string(20, '-') << "↑↑ 响应正文↑↑ " << std::string(20, '-') << std::endl;
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
    }   // */
}

void prepare(int argc, char* argv[])
{
    if (argc < 2 || argv[1] == std::string("-h") || argv[1] == std::string("--help"))
    {
        std::cerr << "usage: " << argv[0] << " <url>" << std::endl;
        std::exit(EXIT_SUCCESS);
    }

#ifdef __unix__
    if (access(_FILE_TXT, F_OK))
    {
        // 生成一个文本文件 用于上传
        std::ofstream stream(_FILE_TXT);
        stream << "lets upload [" _FILE_TXT "]\n";
    }

    if (access(_FILE_IMG, F_OK))
    {
        // 生成一个图片文件 用于上传
        std::FILE* fp = std::fopen(_FILE_IMG, "wb");
        if (fp)
        {
#   if defined(__GNUC__) && __GNUC__ >= 4 && __GNUC_MINOR__ >= 6 && !defined(__clang__)
#       pragma GCC diagnostic push
#       pragma GCC diagnostic ignored "-Wnarrowing"
#   endif
            const char content[] = {
                0xff,0xd8,0xff,0xe0,0x0,0x10,0x4a,0x46,0x49,0x46,0x0,0x1,0x1,0x0,
                0x0,0x1,0x0,0x1,0x0,0x0,0xff,0xdb,0x0,0x84,0x0,0x6,0x4,0x5,0x6,0x5,0x4,0x6,0x6,0x5,0x6,
                0x7,0x7,0x6,0x8,0xa,0x10,0xa,0xa,0x9,0x9,0xa,0x14,0xe,0xf,0xc,0x10,0x17,0x14,0x18,0x18,
                0x17,0x14,0x16,0x16,0x1a,0x1d,0x25,0x1f,0x1a,0x1b,0x23,0x1c,0x16,0x16,0x20,0x2c,0x20,
                0x23,0x26,0x27,0x29,0x2a,0x29,0x19,0x1f,0x2d,0x30,0x2d,0x28,0x30,0x25,0x28,0x29,0x28,
                0x1,0x7,0x7,0x7,0xa,0x8,0xa,0x13,0xa,0xa,0x13,0x28,0x1a,0x16,0x1a,0x28,0x28,0x28,0x28,
                0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,
                0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,
                0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,0xff,0xc0,0x0,0x11,0x8,
                0x0,0x1,0x0,0x1,0x3,0x1,0x11,0x0,0x2,0x11,0x1,0x3,0x11,0x1,0xff,0xc4,0x1,0xa2,0x0,
                0x0,0x1,0x5,0x1,0x1,0x1,0x1,0x1,0x1,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x1,0x2,0x3,0x4,
                0x5,0x6,0x7,0x8,0x9,0xa,0xb,0x10,0x0,0x2,0x1,0x3,0x3,0x2,0x4,0x3,0x5,0x5,0x4,0x4,0x0,
                0x0,0x1,0x7d,0x1,0x2,0x3,0x0,0x4,0x11,0x5,0x12,0x21,0x31,0x41,0x6,0x13,0x51,0x61,0x7,
                0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x8,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,0x24,
                0x33,0x62,0x72,0x82,0x9,0xa,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,0x29,0x2a,
                0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,
                0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,
                0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x92,0x93,0x94,0x95,
                0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,
                0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,
                0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,
                0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0x1,0x0,0x3,0x1,0x1,0x1,0x1,0x1,0x1,
                0x1,0x1,0x1,0x0,0x0,0x0,0x0,0x0,0x0,0x1,0x2,0x3,0x4,0x5,0x6,0x7,0x8,0x9,0xa,0xb,0x11,
                0x0,0x2,0x1,0x2,0x4,0x4,0x3,0x4,0x7,0x5,0x4,0x4,0x0,0x1,0x2,0x77,0x0,0x1,0x2,0x3,0x11,
                0x4,0x5,0x21,0x31,0x6,0x12,0x41,0x51,0x7,0x61,0x71,0x13,0x22,0x32,0x81,0x8,0x14,0x42,
                0x91,0xa1,0xb1,0xc1,0x9,0x23,0x33,0x52,0xf0,0x15,0x62,0x72,0xd1,0xa,0x16,0x24,0x34,
                0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,
                0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,
                0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,
                0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,
                0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,
                0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,
                0xd9,0xda,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,
                0xf8,0xf9,0xfa,0xff,0xda,0x0,0xc,0x3,0x1,0x0,0x2,0x11,0x3,0x11,0x0,0x3f,0x0,0xf9,0x6a,
                0xee,0xe6,0x7b,0xcb,0xa9,0xae,0xaf,0x26,0x96,0x7b,0x99,0x9d,0xa4,0x96,0x59,0x5c,0xb3,
                0xc8,0xec,0x72,0x59,0x89,0xe4,0x92,0x49,0x24,0x9a,0x0,0xff,0x0,0xff,0xd9
            };
#   if defined(__GNUC__) && __GNUC__ >= 4 && __GNUC_MINOR__ >= 6 && !defined(__clang__)
#       pragma GCC diagnostic pop
#   endif
            std::fwrite(content, sizeof(*content), sizeof(content), fp);
            std::fflush(fp);
        }
    }
#endif
}

#undef _FILE_TXT
#undef _FILE_IMG
