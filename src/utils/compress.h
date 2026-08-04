#pragma once

#include <string>

namespace utils
{

    class Compress
    {
    public:
        virtual ~Compress() = default;

        virtual size_t compress(const void *input, size_t input_size, void *output, size_t output_size) = 0;

        virtual size_t compressed_bound(size_t input_size) = 0;

        virtual std::string decompress(const void *data, size_t size) = 0;

        virtual void reset_stream() = 0;
    };
}