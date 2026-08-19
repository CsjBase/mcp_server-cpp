#pragma once

#include "logger/rotater/RotatedFileHandler.h"
#include "utils/crypt.h"
#include "utils/aes_crypt.h"

#include <memory>
#include <string>

namespace logger
{

    /// 加密处理器：依赖 utils::Crypt 抽象接口，不绑定具体算法。
    /// 注入 AesCrypt 即为 AES-256-GCM，后续替换其它实现无需改动此类。
    class EncryptHandler : public RotatedFileHandler
    {
    public:
        explicit EncryptHandler(std::unique_ptr<utils::Crypt> crypt);

        std::string handle(const std::string &filepath) override;
        std::string suffix() const override { return ".enc"; }

        std::unique_ptr<RotatedFileHandler> clone() const override
        {
            return std::make_unique<EncryptHandler>(crypt_->clone());
        }

        /// 供 CLI 工具复用的静态辅助方法
        static void encrypt_file(const std::string &in_path, const std::string &out_path,
                                 utils::Crypt &crypt);
        static void decrypt_file(const std::string &in_path, const std::string &out_path,
                                 utils::Crypt &crypt);

        ~EncryptHandler() override = default;

    private:
        std::unique_ptr<utils::Crypt> crypt_;
    };

    std::unique_ptr<RotatedFileHandler> create_encrypt_handler(const std::string &key);

} // namespace logger
