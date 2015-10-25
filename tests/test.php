<?php
/**
 * convention：
 * POST 请求不会有 GET 参数；
 * 不考虑 键名 重复的情况，这种情况签名校验不通过；
 * 签名前一律按键名做升序排列（除非特别说明）；
 */
class Signer
{
    const SIGN_SECRET = 'FJDFf*e^fegffdh&^gfbvoi&*jf|{{kdm(9';  // 密钥，与客户端保持一致
    const SIGN_HYPHEN = '|',    // 签名字符片段的连接符
        SIGN_KEY = 'sign';      // 请求参数中签名参数的键名

    const VERSION_KEY = 'protocol_version';     // 协议版本号 的键名。见 $_GET['protocol_version'] 或 $_POST['protocol_version'] 。
    const USER_AGENT = 'BGY-KaoQinJi';          // http头中的 User-Agent 值。见 $_SERVER['HTTP_USER_AGENT'] 。

    public function sign()
    {
        switch ($_SERVER['REQUEST_METHOD'])
        {
            case 'GET':
                return $this->genSign($_GET, true);
            case 'POST':
                if ($_FILES)
                {
                    return $this->signUpload();
                }
                else
                {
                    return $this->genSign($_POST, true);
                }
            default:
                return null;
        }
    }

    /**
     * 数组参数的签名值
     */
    private function genSign(array $params, $withUserAgent = false)
    {
        // 按键名做字典序升序排列：
        ksort($params, SORT_ASC | SORT_STRING);

        // 拼接得到 $serializedParams： key_1=value_1&key_2=value_2 ...
        $segments = array();
        foreach ($params as $key => &$val)
        {
            if ($key != self::SIGN_KEY)
            {
                $segments[] = $key . '=' . $val;
            }
        }
        $serializedParams = implode('&', $segments);

        // 生成签名值： md5( SIGN_SECRET + SIGN_HYPHEN + User-Agent + SIGN_HYPHEN + $serializedParams )
        return md5(self::SIGN_SECRET . self::SIGN_HYPHEN
            . ($withUserAgent ? $_SERVER['HTTP_USER_AGENT'] . self::SIGN_HYPHEN : '')
            . $serializedParams);
    }

    /**
     * 有上传文件的 POST 请求，签名算法：
     *
     * ① 取 POST 参数的签名值 s1：
     * s1 = sign($_POST)
     *
     * ② 取文件参数的签名值：
     * 例如： $_FILES = {
     *                     'key-1' => { 'name' => 'abc.jpg', 'tmp_name' => '/tmp/phpcFcG7f' ... },
     *                     'key-2' => { 'name' => 'xyz.png', 'tmp_name' => '/tmp/phpcT78hr' ... },
     *                 ... }
     * 取 file-params = { 'key-1' => 'abc.jpg', 'key-2' => 'xyz.png' ... }
     * 取 s2 = sign( file-params ) 作为文件参数的签名值
     *
     * ③ 取所有文件的 md5 签名值：
     * s3 = signFile( file-1 ), s4 = signFile( file-2 ) ...
     *
     * ④ 以上所有签名值组成数组，按签名值做升序排列，再取签名。
     * sign = md5( [ s1, s2, s3, s4 ... ] );
     */
    private function signUpload()
    {
        $signs = array();

        // ①：
        $signs[] = $this->genSign($_POST, true);

        // ②：
        $uploadParams = array();
        foreach ($_FILES as $key => &$f)
        {
            $uploadParams[$key] = $f['name'];
        }
        $signs[] = $this->genSign($uploadParams, false);    // 文件参数签名不加 User-Agent

        // ③：
        foreach ($_FILES as &$f)
        {
            $signs[] = $this->signFile($f['tmp_name']);
        }

        // ④：
        sort($signs, SORT_ASC | SORT_STRING);
        return md5(self::SIGN_SECRET . self::SIGN_HYPHEN . implode(self::SIGN_HYPHEN, $signs));
    }

    /**
     * 计算文件的签名值。（NOTE：文件的签名值 直接取md5值，不加 SIGN_SECRET）
     */
    private function signFile($file)
    {
        if (!file_exists($file))
        {
            throw new Exception("file [{$file}] non-exists", 10086);
        }
        return md5(file_get_contents($file));
    }
}

//*
print_r($_GET);
print_r($_POST);
print_r($_FILES);
print_r(getallheaders());
print_r($_COOKIE);
// */
$sign = (new Signer())->sign();
echo ($sign === $_REQUEST[Signer::SIGN_KEY]) ? 'success' : "failure: [{$sign}]";
