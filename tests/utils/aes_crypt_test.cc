#include "utils/aes_crypt.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

using utils::AesCrypt;

// ---- round-trip ----
TEST(AesCryptTest, RoundTripSmall)
{
    AesCrypt crypt("test-key");
    std::string plain = "Hello, AES-256-GCM!";

    crypt.reset();
    size_t bound = crypt.encrypt_bound(plain.size());
    std::vector<char> cipher(bound);
    size_t cipher_len = crypt.encrypt(plain.data(), plain.size(), cipher.data(), bound);
    ASSERT_GT(cipher_len, 0u);

    crypt.reset();
    std::vector<char> decrypted(plain.size() + 64);
    size_t dec_len = crypt.decrypt(cipher.data(), cipher_len, decrypted.data(), decrypted.size());
    ASSERT_GT(dec_len, 0u);

    EXPECT_EQ(std::string(decrypted.data(), dec_len), plain);
}

TEST(AesCryptTest, RoundTripEmpty)
{
    AesCrypt crypt("key");
    std::string plain;

    crypt.reset();
    size_t bound = crypt.encrypt_bound(plain.size());
    std::vector<char> cipher(bound);
    size_t cipher_len = crypt.encrypt(plain.data(), plain.size(), cipher.data(), bound);
    // 加密空数据仍应成功（有 IV + Tag）
    ASSERT_GT(cipher_len, 0u);

    crypt.reset();
    std::vector<char> decrypted(64);
    size_t dec_len = crypt.decrypt(cipher.data(), cipher_len, decrypted.data(), decrypted.size());
    EXPECT_EQ(dec_len, 0u);
}

TEST(AesCryptTest, RoundTripLarge)
{
    AesCrypt crypt("my-secret-password");
    std::vector<char> plain(100000);
    for (size_t i = 0; i < plain.size(); ++i)
        plain[i] = static_cast<char>(i % 256);

    crypt.reset();
    size_t bound = crypt.encrypt_bound(plain.size());
    std::vector<char> cipher(bound);
    size_t cipher_len = crypt.encrypt(plain.data(), plain.size(), cipher.data(), bound);
    ASSERT_GT(cipher_len, 0u);

    crypt.reset();
    std::vector<char> decrypted(plain.size() + 64);
    size_t dec_len = crypt.decrypt(cipher.data(), cipher_len, decrypted.data(), decrypted.size());
    ASSERT_EQ(dec_len, plain.size());
    EXPECT_EQ(std::memcmp(decrypted.data(), plain.data(), plain.size()), 0);
}

// ---- wrong key ----
TEST(AesCryptTest, WrongKeyFails)
{
    AesCrypt crypt1("correct-key");
    AesCrypt crypt2("wrong-key");
    std::string plain = "sensitive data";

    crypt1.reset();
    size_t bound = crypt1.encrypt_bound(plain.size());
    std::vector<char> cipher(bound);
    size_t cipher_len = crypt1.encrypt(plain.data(), plain.size(), cipher.data(), bound);
    ASSERT_GT(cipher_len, 0u);

    crypt2.reset();
    std::vector<char> decrypted(plain.size() + 64);
    size_t dec_len = crypt2.decrypt(cipher.data(), cipher_len, decrypted.data(), decrypted.size());
    EXPECT_EQ(dec_len, 0u);
}

// ---- corrupted data ----
TEST(AesCryptTest, CorruptedDataFails)
{
    AesCrypt crypt("key");
    std::string plain = "tamper-proof data";

    crypt.reset();
    size_t bound = crypt.encrypt_bound(plain.size());
    std::vector<char> cipher(bound);
    size_t cipher_len = crypt.encrypt(plain.data(), plain.size(), cipher.data(), bound);
    ASSERT_GT(cipher_len, 0u);

    // 翻转密文中的一个字节
    cipher[cipher_len / 2] ^= 0x01;

    crypt.reset();
    std::vector<char> decrypted(plain.size() + 64);
    size_t dec_len = crypt.decrypt(cipher.data(), cipher_len, decrypted.data(), decrypted.size());
    EXPECT_EQ(dec_len, 0u);
}

// ---- tampered tag ----
TEST(AesCryptTest, TamperedTagFails)
{
    AesCrypt crypt("key");
    std::string plain = "authenticated data";

    crypt.reset();
    size_t bound = crypt.encrypt_bound(plain.size());
    std::vector<char> cipher(bound);
    size_t cipher_len = crypt.encrypt(plain.data(), plain.size(), cipher.data(), bound);
    ASSERT_GT(cipher_len, 0u);

    // 修改最后一个字节（GCM Tag 内）
    cipher[cipher_len - 1] ^= 0xFF;

    crypt.reset();
    std::vector<char> decrypted(plain.size() + 64);
    size_t dec_len = crypt.decrypt(cipher.data(), cipher_len, decrypted.data(), decrypted.size());
    EXPECT_EQ(dec_len, 0u);
}

// ---- encrypt_bound ----
TEST(AesCryptTest, EncryptBoundIsSufficient)
{
    AesCrypt crypt("key");
    std::vector<size_t> sizes = {0, 1, 16, 256, 4096, 65536};
    for (auto sz : sizes)
    {
        size_t bound = crypt.encrypt_bound(sz);
        EXPECT_GE(bound, sz + AesCrypt::kIvLen + AesCrypt::kTagLen)
            << "bound " << bound << " too small for size " << sz;
    }
}

// ---- reset produces different IV ----
TEST(AesCryptTest, ResetGeneratesNewIv)
{
    AesCrypt crypt("key");
    std::string plain = "same plaintext";

    crypt.reset();
    size_t bound = crypt.encrypt_bound(plain.size());
    std::vector<char> c1(bound);
    size_t len1 = crypt.encrypt(plain.data(), plain.size(), c1.data(), bound);
    ASSERT_GT(len1, 0u);

    crypt.reset();
    std::vector<char> c2(bound);
    size_t len2 = crypt.encrypt(plain.data(), plain.size(), c2.data(), bound);
    ASSERT_GT(len2, 0u);
    ASSERT_EQ(len1, len2);

    // 两次加密的 IV（前 12 字节）应不同
    EXPECT_NE(std::memcmp(c1.data(), c2.data(), AesCrypt::kIvLen), 0);
}

// ---- different keys produce different ciphertext ----
TEST(AesCryptTest, DifferentKeysDifferentOutput)
{
    AesCrypt crypt_a("key-a");
    AesCrypt crypt_b("key-b");
    std::string plain = "test data";

    crypt_a.reset();
    size_t bound = crypt_a.encrypt_bound(plain.size());
    std::vector<char> c1(bound);
    size_t len1 = crypt_a.encrypt(plain.data(), plain.size(), c1.data(), bound);
    ASSERT_GT(len1, 0u);

    crypt_b.reset();
    std::vector<char> c2(bound);
    size_t len2 = crypt_b.encrypt(plain.data(), plain.size(), c2.data(), bound);
    ASSERT_GT(len2, 0u);
    ASSERT_EQ(len1, len2);

    // 密文（跳过 IV 部分，因为 IV 本来就随机）中的加密数据应不同
    size_t data_offset = AesCrypt::kIvLen;
    size_t data_len = len1 - data_offset;
    bool different = std::memcmp(c1.data() + data_offset, c2.data() + data_offset, data_len) != 0;
    EXPECT_TRUE(different);
}

// ---- nil input/output ----
TEST(AesCryptTest, NullPointerReturnsZero)
{
    AesCrypt crypt("key");
    char buf[128];
    // Both null
    EXPECT_EQ(crypt.encrypt(nullptr, 10, buf, sizeof(buf)), 0u);
    EXPECT_EQ(crypt.decrypt(nullptr, 10, buf, sizeof(buf)), 0u);
    // Output null
    EXPECT_EQ(crypt.encrypt(buf, 10, nullptr, sizeof(buf)), 0u);
    EXPECT_EQ(crypt.decrypt(buf, 10, nullptr, sizeof(buf)), 0u);
}

// ---- input too small ----
TEST(AesCryptTest, InputTooSmall)
{
    AesCrypt crypt("key");
    char buf[64];
    // 小于 IV + Tag 的数据不可能有效
    size_t small = static_cast<size_t>(AesCrypt::kIvLen + AesCrypt::kTagLen - 1);
    EXPECT_EQ(crypt.decrypt(buf, small, buf, sizeof(buf)), 0u);
}
