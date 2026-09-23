#pragma once

#include <openssl/rand.h>
#include <openssl/evp.h>
#include <string>

std::string generateSessionId()
{
    unsigned char buf[16];
    RAND_bytes(buf, sizeof(buf));

    // base64url 编码，去掉 padding
    std::string out;
    out.resize(4 * ((sizeof(buf) + 2) / 3));
    int len = EVP_EncodeBlock(
        reinterpret_cast<unsigned char *>(&out[0]), buf, sizeof(buf));
    out.resize(len);

    // 标准 base64 → base64url
    for (char &c : out)
    {
        if (c == '+')
            c = '-';
        else if (c == '/')
            c = '_';
    }
    while (!out.empty() && out.back() == '=')
        out.pop_back();
    return out; // 22 字符，URL/Header 安全
}
