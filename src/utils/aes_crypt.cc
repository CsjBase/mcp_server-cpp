#include "utils/aes_crypt.h"

#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#include <cstring>
#include <stdexcept>
#include <sys/random.h>

namespace utils
{

    namespace
    {
        constexpr int kAes256KeyLen = 32;

        // OpenSSL 静态链接时，自动初始化机制可能在线程池 worker 创建前未能触发，
        // 导致 worker 线程中的 EVP 调用失败且错误队列为空。此处确保 OpenSSL 在库加载时初始化。
        struct OpenSSLInitializer
        {
            OpenSSLInitializer() { OPENSSL_init_crypto(OPENSSL_INIT_NO_ATEXIT, nullptr); }
        };
        static const OpenSSLInitializer g_openssl_init;

        void throw_openssl_error(unsigned long err_code)
        {
            if (err_code == 0)
            {
                throw std::runtime_error("AesCrypt: OpenSSL error — no error in queue");
            }

            char buf[256];
            ERR_error_string_n(err_code, buf, sizeof(buf));
            throw std::runtime_error(std::string("AesCrypt: OpenSSL error [0x") +
                                     std::to_string(err_code) + "] — " + buf);
        }
    } // namespace

    AesCrypt::AesCrypt(std::string key)
        : user_key_(key), derived_key_(derive_key(key)), iv_(new unsigned char[kIvLen])
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

        if (!iv_generated_)
        {
            // if (getrandom(iv_.get(), kIvLen, 0) != static_cast<ssize_t>(kIvLen))
            if (RAND_bytes(iv_.get(), kIvLen) != 1)
            {
                throw std::runtime_error("AesCrypt: getrandom() failed for IV generation");
            }
            iv_generated_ = true;
        }

        size_t min_out = kIvLen + input_size + kTagLen;
        if (output_size < min_out)
            return 0;

        auto *out = static_cast<unsigned char *>(output);
        std::memcpy(out, iv_.get(), kIvLen);

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
        {
            unsigned long err_code = ERR_get_error();
            throw_openssl_error(err_code);
        }

        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                               reinterpret_cast<const unsigned char *>(derived_key_.data()),
                               iv) != 1)
        {
            unsigned long err_code = ERR_get_error();
            EVP_CIPHER_CTX_free(ctx);
            throw_openssl_error(err_code);
        }

        int len = 0;
        if (EVP_EncryptUpdate(ctx, out, &len, plain, static_cast<int>(plain_len)) != 1)
        {
            unsigned long err_code = ERR_get_error();
            EVP_CIPHER_CTX_free(ctx);
            throw_openssl_error(err_code);
        }
        out_len = static_cast<size_t>(len);

        if (EVP_EncryptFinal_ex(ctx, out + out_len, &len) != 1)
        {
            unsigned long err_code = ERR_get_error();
            EVP_CIPHER_CTX_free(ctx);
            throw_openssl_error(err_code);
        }
        out_len += static_cast<size_t>(len);

        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagLen, out + out_len) != 1)
        {
            unsigned long err_code = ERR_get_error();
            EVP_CIPHER_CTX_free(ctx);
            throw_openssl_error(err_code);
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
        {
            unsigned long err_code = ERR_get_error();
            throw_openssl_error(err_code);
        }

        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                               reinterpret_cast<const unsigned char *>(derived_key_.data()),
                               iv) != 1)
        {
            unsigned long err_code = ERR_get_error();
            EVP_CIPHER_CTX_free(ctx);
            throw_openssl_error(err_code);
        }

        int len = 0;
        if (EVP_DecryptUpdate(ctx, out, &len, cipher, static_cast<int>(cipher_len)) != 1)
        {
            unsigned long err_code = ERR_get_error();
            EVP_CIPHER_CTX_free(ctx);
            throw_openssl_error(err_code);
        }
        out_len = static_cast<size_t>(len);

        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagLen,
                                const_cast<unsigned char *>(tag)) != 1)
        {
            unsigned long err_code = ERR_get_error();
            EVP_CIPHER_CTX_free(ctx);
            throw_openssl_error(err_code);
        }

        int ret = EVP_DecryptFinal_ex(ctx, out + out_len, &len);
        EVP_CIPHER_CTX_free(ctx);
        if (ret != 1)
            return false;

        out_len += static_cast<size_t>(len);
        return true;
    }

} // namespace utils
