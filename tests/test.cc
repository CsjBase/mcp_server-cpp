// 测试代码
#include <openssl/rand.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "utils/thread_pool.h"

static int cnt = 0;
const int iterations = 10000;
// struct OpenSSLInitializer
// {
//     OpenSSLInitializer() { OPENSSL_init_crypto(OPENSSL_INIT_NO_ATEXIT, nullptr); }
// };
// static const OpenSSLInitializer g_openssl_init;
void func()
{
    const int kIvLen = 12;
    std::unique_ptr<unsigned char[]> iv_(new unsigned char[kIvLen]);
    if (RAND_bytes(iv_.get(), kIvLen) != 1)
    {
        std::cout << "RAND_bytes() failed，在第" << cnt << "个" << std::endl;
        throw std::runtime_error("RAND_bytes() failed");
    }
    cnt++;
    if (cnt == iterations)
    {
        std::cout << "成功" << iterations << "个" << std::endl;
    }
}

std::unique_ptr<utils::ThreadPool> thread_pool_ = std::make_unique<utils::ThreadPool>(1, 1000);

int main()
{
    for (int i = 0; i < iterations; i++)
    {
        thread_pool_->submit(func);
    }
}