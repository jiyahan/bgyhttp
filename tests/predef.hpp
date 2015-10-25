
/**
 * 配置项。缺省定义见 bgy/predef.hpp 。
 */

#pragma once


// 签名用的密钥，与服务器端保持一致，每台设备都不一样。
#define BGY_SECRET                   "FJDFf*e^fegffdh&^gfbvoi&*jf|{{kdm(9"

// convention
#define BGY_PROTOCOL_VERSION_MAJOR   0        // 主版本号
#define BGY_PROTOCOL_VERSION_MINOR   1        // 分支版本号
#define BGY_PROTOCOL_VERSION_PATCH   0        // 补丁版本号
#define BGY_CONNECT_TIMEOUT          10              // curl连接超时时间（秒）
#define BGY_REQUEST_TIMEOUT          60              // curl请求超时时间（秒）
#define BGY_USER_AGENT               "BGY-KaoQinJi"      // http头 User-Agent 值
#define BGY_PROTOCOL_VERSION_KEY     "protocol_version"      // 协议版本号参数 键名
#define BGY_SIGN_KEY                 "sign"     // 签名参数 键名
#define BGY_URL_MAX_LENGTH           4096       // URL 最大长度
#define BGY_SIGN_HYPHEN              "|"        // 签名字符片段连接符
#define BGY_RESPONSE_MAX_CONTENT_LENGTH     INT_MAX     // http响应中 Content-Length 最大值，超过此值请求不会被处理。
#define BGY_FREAD_BUFFER_SIZE        4096       // 读文件时 buffer 字节数（NOTE：栈上分配）
