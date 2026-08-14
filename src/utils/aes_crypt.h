#pragma once

#include "utils/crypt.h"

#include <memory>
#include <string>

namespace utils
{

    /// AES-256-GCM 实现。
    /// 每次 encrypt() 自动生成随机 IV，输出格式：[12B IV][密文][16B GCM Tag]
    class AesCrypt : public Crypt
    {
    public:
        /// @param key 用户密钥（内部通过 SHA-256 派生为 32 字节 AES-256 密钥）
        explicit AesCrypt(std::string key);
        ~AesCrypt() override;

        size_t encrypt(const void *input, size_t input_size, void *output, size_t output_size) override;
        size_t decrypt(const void *input, size_t input_size, void *output, size_t output_size) override;
        size_t encrypt_bound(size_t input_size) override;
        void reset() override;

        std::unique_ptr<Crypt> clone() const override
        {
            return std::make_unique<AesCrypt>(user_key_);
        }

        static constexpr int kIvLen = 12;
        static constexpr int kTagLen = 16;

    private:
        static std::string derive_key(const std::string &user_key);
        bool encrypt_impl(const unsigned char *plain, size_t plain_len,
                          unsigned char *out, size_t out_size, size_t &out_len,
                          const unsigned char *iv);
        bool decrypt_impl(const unsigned char *cipher, size_t cipher_len,
                          unsigned char *out, size_t out_size, size_t &out_len,
                          const unsigned char *iv, const unsigned char *tag);

        std::string user_key_;
        std::string derived_key_;
        std::unique_ptr<unsigned char[]> iv_;
        bool iv_generated_ = false;
    };

} // namespace utils
