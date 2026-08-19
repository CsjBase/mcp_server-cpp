#include "logger/rotater/EncryptHandler.h"
#include "logger/public.h"

#include <cstdio>
#include <fstream>
#include <vector>

namespace logger
{

    EncryptHandler::EncryptHandler(std::unique_ptr<utils::Crypt> crypt)
        : crypt_(std::move(crypt)) {}

    void EncryptHandler::encrypt_file(const std::string &in_path,
                                      const std::string &out_path,
                                      utils::Crypt &crypt)
    {
        std::ifstream in(in_path, std::ios::binary | std::ios::ate);
        if (!in)
            throw LogException("EncryptHandler: failed to open " + in_path);

        size_t plain_len = static_cast<size_t>(in.tellg());
        in.seekg(0);
        std::vector<char> plain(plain_len);
        in.read(plain.data(), static_cast<std::streamsize>(plain_len));
        in.close();

        crypt.reset();
        size_t bound = crypt.encrypt_bound(plain_len);
        std::vector<char> cipher(bound);
        size_t out_len = crypt.encrypt(plain.data(), plain_len, cipher.data(), bound);
        if (out_len == 0)
            throw LogException("EncryptHandler: encryption failed for " + in_path);

        std::ofstream out(out_path, std::ios::binary);
        if (!out)
            throw LogException("EncryptHandler: failed to create " + out_path);
        out.write(cipher.data(), static_cast<std::streamsize>(out_len));
        out.close();
    }

    void EncryptHandler::decrypt_file(const std::string &in_path,
                                      const std::string &out_path,
                                      utils::Crypt &crypt)
    {
        std::ifstream in(in_path, std::ios::binary | std::ios::ate);
        if (!in)
            throw LogException("EncryptHandler: failed to open " + in_path);
        size_t total = static_cast<size_t>(in.tellg());
        in.seekg(0);
        std::vector<char> data(total);
        in.read(data.data(), static_cast<std::streamsize>(total));
        in.close();

        crypt.reset();
        std::vector<char> plain(total); // 解密后 <= 输入
        size_t out_len = crypt.decrypt(data.data(), total, plain.data(), plain.size());
        if (out_len == 0)
            throw LogException("EncryptHandler: decryption failed — wrong key or corrupted file");

        std::ofstream out(out_path, std::ios::binary);
        if (!out)
            throw LogException("EncryptHandler: failed to create " + out_path);
        out.write(plain.data(), static_cast<std::streamsize>(out_len));
        out.close();
    }

    std::string EncryptHandler::handle(const std::string &filepath)
    {
        std::string out_path = filepath + suffix();
        encrypt_file(filepath, out_path, *crypt_);
        if (std::remove(filepath.c_str()) != 0)
            throw LogException("EncryptHandler: failed to remove " + filepath, errno);
        return out_path;
    }

    std::unique_ptr<RotatedFileHandler> create_encrypt_handler(const std::string &key)
    {
        return std::make_unique<EncryptHandler>(std::make_unique<utils::AesCrypt>(key));
    }

} // namespace logger
