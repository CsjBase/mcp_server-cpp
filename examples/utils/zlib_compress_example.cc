#include "utils/zlib_compress.h"

#include <iostream>
#include <string>
#include <vector>

int main()
{
    utils::ZlibCompress zlib;

    // 1. 压缩字符串
    std::string original = "Hello, zlib! This is a test message that will be compressed. "
                           "zlib compression is useful for reducing log file size on disk.";
    std::cout << "Original: " << original.size() << " bytes\n";

    zlib.reset_stream();
    size_t bound = zlib.compressed_bound(original.size());
    std::vector<char> compressed(bound);
    size_t out_len = zlib.compress(original.data(), original.size(),
                                   compressed.data(), bound);
    if (out_len == 0)
    {
        std::cerr << "Compression failed!\n";
        return 1;
    }
    std::cout << "Compressed: " << out_len << " bytes "
              << "(" << (out_len * 100 / original.size()) << "% of original)\n";

    // 2. 解压
    std::string decompressed = zlib.decompress(compressed.data(), out_len);
    if (decompressed.empty())
    {
        std::cerr << "Decompression failed!\n";
        return 1;
    }
    std::cout << "Decompressed: " << decompressed.size() << " bytes\n";

    if (decompressed == original)
    {
        std::cout << "Round-trip OK!\n";
    }

    // 3. 重复压缩不同大小的数据
    std::cout << "\n--- Compression ratio by size ---\n";
    for (size_t sz : {64, 256, 1024, 4096, 65536})
    {
        std::string data(sz, 'A'); // 高度可压缩
        zlib.reset_stream();
        bound = zlib.compressed_bound(sz);
        compressed.resize(bound);
        out_len = zlib.compress(data.data(), sz, compressed.data(), bound);
        std::cout << sz << " -> " << out_len << " bytes ("
                  << (out_len * 100 / sz) << "%)\n";
    }

    return 0;
}
