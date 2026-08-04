#pragma once

#include <cstddef>

namespace utils
{

    /// 对称加密抽象接口，对标 utils::Compress。
    /// 实现类负责具体的加密算法（AES-GCM、ChaCha20 等）。
    class Crypt
    {
    public:
        virtual ~Crypt() = default;

        /// 加密。input/output 可为同一缓冲区（原地加密）。
        /// @return 实际输出字节数，0 表示失败。
        virtual size_t encrypt(const void *input, size_t input_size, void *output, size_t output_size) = 0;

        /// 解密。input/output 可为同一缓冲区。
        /// @return 实际输出字节数，0 表示失败（密钥错误或数据损坏）。
        virtual size_t decrypt(const void *input, size_t input_size, void *output, size_t output_size) = 0;

        /// 密文上限（明文 + IV + Tag + 块对齐）
        virtual size_t encrypt_bound(size_t input_size) = 0;

        /// 重置加密状态（生成新 IV / Nonce）
        virtual void reset() = 0;
    };

} // namespace utils
