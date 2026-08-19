#pragma once

#include "utils/compress.h"

#include <zlib.h>
#include <memory>

namespace utils
{
    struct ZStreamDeflateDeleter
    {
        void operator()(z_stream *stream)
        {
            if (stream)
            {
                deflateEnd(stream);
                delete stream;
            }
        }
    };

    struct ZStreamInflateDeleter
    {
        void operator()(z_stream *stream)
        {
            if (stream)
            {
                inflateEnd(stream);
                delete stream;
            }
        }
    };

    class ZlibCompress final : public Compress
    {
    public:
        ZlibCompress() = default;
        ~ZlibCompress() override = default;
        size_t compress(const void *input, size_t input_size, void *output, size_t output_size) override;

        std::string decompress(const void *data, size_t size) override;

        void reset_stream() override;

        size_t compressed_bound(size_t input_size) override;

        std::unique_ptr<Compress> clone() const override
        {
            return std::make_unique<ZlibCompress>();
        }

    private:
        void reset_uncompress_stream_();

    private:
        std::unique_ptr<z_stream, ZStreamDeflateDeleter> compress_stream_;
        std::unique_ptr<z_stream, ZStreamInflateDeleter> uncompress_stream_;
    };

}