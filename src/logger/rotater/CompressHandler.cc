#include "logger/rotater/CompressHandler.h"
#include "logger/public.h"

#include <cstdio>
#include <fstream>
#include <vector>

namespace logger
{

    CompressHandler::CompressHandler(std::unique_ptr<utils::Compress> compressor)
        : compressor_(std::move(compressor))
    {
        compressor_->reset_stream();
    }

    std::string CompressHandler::handle(const std::string &filepath)
    {
        std::ifstream in(filepath, std::ios::binary | std::ios::ate);
        if (!in)
        {
            throw LogException("CompressHandler: failed to open " + filepath);
        }
        size_t input_size = static_cast<size_t>(in.tellg());
        in.seekg(0, std::ios::beg);

        std::string out_path = filepath + suffix();
        std::ofstream out(out_path, std::ios::binary);
        if (!out)
        {
            throw LogException("CompressHandler: failed to create " + out_path);
        }

        // 输出缓冲按块大小预留: 分块压缩内存 O(块大小), 大轮转文件下
        // 不再出现 ~2x 文件大小的瞬态内存峰值
        constexpr size_t kChunkSize = 1 << 20; // 1MB
        std::vector<char> output(compressor_->compressed_bound(kChunkSize));

        if (input_size == 0)
        {
            // 空文件: 产出合法空 zlib 流(头部+同步标记), 解压端语义一致,
            // 处理链的 rename 不会因产物缺失而断裂
            char dummy = 0;
            compressor_->reset_stream();
            size_t compressed = compressor_->compress(&dummy, 0, output.data(), output.size());
            if (compressed == 0)
            {
                throw LogException("CompressHandler: compression failed for " + filepath);
            }
            out.write(output.data(), static_cast<std::streamsize>(compressed));
        }
        else
        {
            // 分块压缩: 每次 reset_stream 产出一条独立 zlib 流,
            // 多条流拼接 inflate 可自动接续解压(格式兼容)
            std::vector<char> input(kChunkSize);
            while (in)
            {
                in.read(input.data(), static_cast<std::streamsize>(kChunkSize));
                std::streamsize n = in.gcount();
                if (n == 0)
                    break;

                compressor_->reset_stream();
                size_t compressed = compressor_->compress(
                    input.data(), static_cast<size_t>(n), output.data(), output.size());
                if (compressed == 0)
                {
                    throw LogException("CompressHandler: compression failed for " + filepath);
                }
                out.write(output.data(), static_cast<std::streamsize>(compressed));
                if (!out)
                {
                    throw LogException("CompressHandler: failed to write " + out_path);
                }
            }
        }

        in.close();
        out.close();

        // 删除原文件
        if (std::remove(filepath.c_str()) != 0)
        {
            throw LogException("CompressHandler: failed to remove " + filepath, errno);
        }

        return out_path;
    }

    std::unique_ptr<RotatedFileHandler> create_compress_handler(std::unique_ptr<utils::Compress> compressor)
    {
        return std::make_unique<CompressHandler>(std::move(compressor));
    }

} // namespace logger
