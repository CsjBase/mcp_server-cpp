#pragma once

#include "logger/rotater/RotatedFileHandler.h"
#include "utils/compress.h"

#include <memory>

namespace logger
{

    /// 在滚出文件后使用 zlib 进行 gzip 压缩。
    /// handle() 是同步的——大文件时 rotate_() 可能阻塞数百毫秒，
    /// 但日志文件受 max_size 限制，通常可接受。
    class CompressHandler : public RotatedFileHandler
    {
    public:
        explicit CompressHandler(std::unique_ptr<utils::Compress> compressor);

        std::string handle(const std::string &filepath) override;
        std::string suffix() const override { return ".gz"; }

        std::unique_ptr<RotatedFileHandler> clone() const override
        {
            return std::make_unique<CompressHandler>(compressor_->clone());
        }

    private:
        std::unique_ptr<utils::Compress> compressor_;
    };

} // namespace logger
