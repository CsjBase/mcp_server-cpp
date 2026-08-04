#include "utils/aes_crypt.h"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <cstring>
#include <stdexcept>

namespace utils
{

    namespace
    {
        constexpr int kAes256KeyLen = 32;

        void throw_openssl_error()
        {
            char buf[256];
            ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
            throw std::runtime_error(std::string("AesCrypt: OpenSSL error — ") + buf);
        }
    } // namespace

    AesCrypt::AesCrypt(std::string key)
        : derived_key_(derive_key(key)), iv_(new unsigned char[kIvLen])
    {
    }

    AesCrypt::~AesCrypt() = default;

    std::string AesCrypt::derive_key(const std::string &user_key)
    {
        std::string key(kAes256KeyLen, '\0');
        SHA256(reinterpret_cast<const unsigned char *>(user_key.data()),
               user_key.size(),
               reinterpret_cast<unsigned char *>(key.data()));
        return key;
    }

    void AesCrypt::reset()
    {
        iv_generated_ = false;
    }

    size_t AesCrypt::encrypt_bound(size_t input_size)
    {
        return input_size + kIvLen + kTagLen + EVP_MAX_BLOCK_LENGTH;
    }

    size_t AesCrypt::encrypt(const void *input, size_t input_size,
                             void *output, size_t output_size)
    {
        if (!input || !output)
            return 0;

        // 生成随机 IV
        if (!iv_generated_)
        {
            if (RAND_bytes(iv_.get(), kIvLen) != 1)
                throw_openssl_error();
            iv_generated_ = true;
        }

        // 输出格式：[IV][密文][Tag]
        size_t min_out = kIvLen + input_size + kTagLen;
        if (output_size < min_out)
            return 0;

        auto *out = static_cast<unsigned char *>(output);
        std::memcpy(out, iv_.get(), kIvLen);

        // 加密
        size_t cipher_len = 0;
        if (!encrypt_impl(static_cast<const unsigned char *>(input), input_size,
                          out + kIvLen, output_size - kIvLen - kTagLen, cipher_len,
                          iv_.get()))
            return 0;

        return kIvLen + cipher_len;
    }

    bool AesCrypt::encrypt_impl(const unsigned char *plain, size_t plain_len,
                                unsigned char *out, size_t out_size, size_t &out_len,
                                const unsigned char *iv)
    {
        auto ctx = EVP_CIPHER_CTX_new();
        if (!ctx)
            throw_openssl_error();

        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                               reinterpret_cast<const unsigned char *>(derived_key_.data()),
                               iv) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            throw_openssl_error();
        }

        int len = 0;
        if (EVP_EncryptUpdate(ctx, out, &len, plain, static_cast<int>(plain_len)) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            throw_openssl_error();
        }
        out_len = static_cast<size_t>(len);

        if (EVP_EncryptFinal_ex(ctx, out + out_len, &len) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            throw_openssl_error();
        }
        out_len += static_cast<size_t>(len);

        // 获取 GCM Tag 并追加到密文末尾
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagLen, out + out_len) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            throw_openssl_error();
        }
        out_len += kTagLen;

        EVP_CIPHER_CTX_free(ctx);
        return true;
    }

    size_t AesCrypt::decrypt(const void *input, size_t input_size,
                             void *output, size_t output_size)
    {
        if (!input || !output)
            return 0;
        if (input_size < static_cast<size_t>(kIvLen + kTagLen))
            return 0;

        const auto *in = static_cast<const unsigned char *>(input);
        auto *out = static_cast<unsigned char *>(output);

        // 解析：[IV][密文+Tag]
        const auto *iv = in;
        const auto *cipher_and_tag = in + kIvLen;
        size_t cipher_and_tag_len = input_size - kIvLen;
        const auto *tag = in + input_size - kTagLen;
        size_t cipher_len = cipher_and_tag_len - kTagLen;

        if (output_size < cipher_len)
            return 0;

        size_t plain_len = 0;
        if (!decrypt_impl(cipher_and_tag, cipher_len,
                          out, output_size, plain_len, iv, tag))
            return 0;

        return plain_len;
    }

    bool AesCrypt::decrypt_impl(const unsigned char *cipher, size_t cipher_len,
                                unsigned char *out, size_t out_size, size_t &out_len,
                                const unsigned char *iv, const unsigned char *tag)
    {
        auto ctx = EVP_CIPHER_CTX_new();
        if (!ctx)
            throw_openssl_error();

        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                               reinterpret_cast<const unsigned char *>(derived_key_.data()),
                               iv) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            throw_openssl_error();
        }

        int len = 0;
        if (EVP_DecryptUpdate(ctx, out, &len, cipher, static_cast<int>(cipher_len)) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            throw_openssl_error();
        }
        out_len = static_cast<size_t>(len);

        // 设置 GCM Tag 用于验证
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagLen,
                                const_cast<unsigned char *>(tag)) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            throw_openssl_error();
        }

        int ret = EVP_DecryptFinal_ex(ctx, out + out_len, &len);
        EVP_CIPHER_CTX_free(ctx);
        if (ret != 1)
            return false; // 认证失败

        out_len += static_cast<size_t>(len);
        return true;
    }

} // namespace utils
