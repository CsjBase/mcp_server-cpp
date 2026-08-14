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
        // 读取原文件
        std::ifstream in(filepath, std::ios::binary | std::ios::ate);
        if (!in)
        {
            throw LogException("CompressHandler: failed to open " + filepath);
        }
        size_t input_size = static_cast<size_t>(in.tellg());
        in.seekg(0, std::ios::beg);

        std::vector<char> input(input_size);
        in.read(input.data(), static_cast<std::streamsize>(input_size));
        in.close();

        // 分配输出缓冲区
        size_t bound = compressor_->compressed_bound(input_size);
        std::vector<char> output(bound);

        // 压缩
        compressor_->reset_stream();
        size_t compressed_size = compressor_->compress(
            input.data(), input_size, output.data(), bound);
        if (compressed_size == 0)
        {
            throw LogException("CompressHandler: compression failed for " + filepath);
        }

        // 写入压缩文件
        std::string out_path = filepath + suffix();
        std::ofstream out(out_path, std::ios::binary);
        if (!out)
        {
            throw LogException("CompressHandler: failed to create " + out_path);
        }
        out.write(output.data(), static_cast<std::streamsize>(compressed_size));
        out.close();

        // 删除原文件
        if (std::remove(filepath.c_str()) != 0)
        {
            throw LogException("CompressHandler: failed to remove " + filepath, errno);
        }

        return out_path;
    }

} // namespace logger
