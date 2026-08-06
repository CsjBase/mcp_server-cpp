#include "logger/handlers/EncryptHandler.h"
#include "utils/aes_crypt.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    const std::string key = "my-secret-password";
    const std::string plaintext = "Sensitive log entry: user=admin, action=delete, "
                                   "target=production_database, timestamp=2026-08-04T12:00:00Z";
    const std::string out_dir = "/tmp/";

    // 1. 创建 AES-256-GCM 加密器（密钥自动 SHA-256 派生）
    utils::AesCrypt crypt(key);

    std::cout << "=== Buffer-level encrypt/decrypt ===\n";
    std::cout << "Plaintext: " << plaintext.size() << " bytes\n";

    // 2. 加密
    crypt.reset();
    size_t bound = crypt.encrypt_bound(plaintext.size());
    std::vector<char> cipher(bound);
    size_t out_len = crypt.encrypt(plaintext.data(), plaintext.size(),
                                   cipher.data(), bound);
    if (out_len == 0)
    {
        std::cerr << "Encryption failed!\n";
        return 1;
    }
    // overhead = 12B IV + 16B GCM tag
    std::cout << "Ciphertext: " << out_len << " bytes (+"
              << (out_len - plaintext.size()) << " overhead)\n";

    // 3. 解密
    crypt.reset();
    std::vector<char> decrypted(plaintext.size() + 64);
    size_t dec_len = crypt.decrypt(cipher.data(), out_len,
                                   decrypted.data(), decrypted.size());
    if (dec_len == 0)
    {
        std::cerr << "Decryption failed!\n";
        return 1;
    }
    std::cout << "Decrypted: " << dec_len << " bytes\n";

    if (std::string(decrypted.data(), dec_len) == plaintext)
    {
        std::cout << "Round-trip OK!\n";
    }

    // 4. 错误密钥验证 GCM 认证
    std::cout << "\n--- Wrong key test ---\n";
    utils::AesCrypt wrong_crypt("wrong-password");
    wrong_crypt.reset();
    std::vector<char> wrong_result(plaintext.size() + 64);
    size_t wrong_len = wrong_crypt.decrypt(cipher.data(), out_len,
                                           wrong_result.data(), wrong_result.size());
    if (wrong_len == 0)
    {
        std::cout << "Wrong key correctly rejected (GCM tag mismatch).\n";
    }

    // 5. 文件级加解密（通过 EncryptHandler 静态方法）
    std::cout << "\n=== File-level encrypt/decrypt ===\n";

    std::string in_path = out_dir + "aes_example_plain.txt";
    std::string enc_path = in_path + ".enc";
    std::string dec_path = in_path + ".dec";

    // 写文件
    {
        std::ofstream out(in_path);
        out << plaintext;
    }
    std::cout << "Written: " << in_path << "\n";

    // 加密文件
    utils::AesCrypt file_crypt("file-key");
    logger::EncryptHandler::encrypt_file(in_path, enc_path, file_crypt);
    std::cout << "Encrypted: " << enc_path << "\n";

    // 解密文件
    utils::AesCrypt decrypt_crypt("file-key");
    logger::EncryptHandler::decrypt_file(enc_path, dec_path, decrypt_crypt);
    std::cout << "Decrypted: " << dec_path << "\n";

    // 验证
    {
        std::ifstream in(dec_path);
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        if (content == plaintext)
        {
            std::cout << "File round-trip OK!\n";
        }
    }

    return 0;
}
