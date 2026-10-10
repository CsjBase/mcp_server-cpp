#include "utils/zlib_compress.h"

namespace utils
{

    static bool IsCompressed(const void *input, size_t input_size)
    {
        if (!input)
        {
            return false;
        }

        if (input_size < 2)
        {
            return false;
        }

        uint16_t magic = *(uint16_t *)input;
        if (magic == 0x9c78 || magic == 0xda78 || magic == 0x5e78 || magic == 0x0178)
        {
            return true;
        }

        return false;
    }

    size_t ZlibCompress::compress(const void *input, size_t input_size, void *output, size_t output_size)
    {
        if (!input || !output)
        {
            return 0;
        }

        if (!compress_stream_)
        {
            return 0;
        }

        compress_stream_->next_in = (Bytef *)input;
        compress_stream_->avail_in = static_cast<uInt>(input_size);

        compress_stream_->next_out = (Bytef *)(output);
        compress_stream_->avail_out = static_cast<uInt>(output_size);

        // Z_FINISH 收尾为完整 zlib 流(含 adler32 尾): 单次调用即一条
        // 完整流, 分块压缩时多条完整流拼接后 inflate 可自动接续解压
        // (RFC 1950 要求解压端支持拼接流);
        // Z_BUF_ERROR 表示输出缓冲不足且重试无法推进(状态不变),
        // 继续循环只会死循环 —— 直接判失败, 由调用方给出足够大的缓冲
        int ret = deflate(compress_stream_.get(), Z_FINISH);
        if (ret != Z_STREAM_END)
        {
            return 0;
        }

        size_t out_len = output_size - compress_stream_->avail_out;
        return out_len;
    }

    std::string ZlibCompress::decompress(const void *input, size_t input_size)
    {
        if (!input)
        {
            return "";
        }
        if (IsCompressed(input, input_size))
        {
            reset_uncompress_stream_();
        }
        if (!uncompress_stream_)
        {
            return "";
        }
        uncompress_stream_->next_in = (Bytef *)input;
        uncompress_stream_->avail_in = static_cast<uInt>(input_size);

        std::string output;
        while (uncompress_stream_->avail_in > 0)
        {
            char buffer[4096] = {0};
            uncompress_stream_->next_out = (Bytef *)buffer;
            uncompress_stream_->avail_out = sizeof(buffer);
            int ret = inflate(uncompress_stream_.get(), Z_SYNC_FLUSH);
            if (ret == Z_STREAM_END)
            {
                output.append(buffer, sizeof(buffer) - uncompress_stream_->avail_out);
                if (uncompress_stream_->avail_in > 0)
                {
                    // 拼接流(分块压缩产物, 每条都是完整 zlib 流):
                    // 显式重置流后继续解压下一条, 避免同流续接导致的死循环
                    Bytef *rest = uncompress_stream_->next_in;
                    uInt rest_len = uncompress_stream_->avail_in;
                    reset_uncompress_stream_();
                    uncompress_stream_->next_in = rest;
                    uncompress_stream_->avail_in = rest_len;
                    continue;
                }
                break;
            }
            if (ret != Z_OK)
            {
                return "";
            }
            output.append(buffer, sizeof(buffer) - uncompress_stream_->avail_out);
        }

        return output;
    }

    void ZlibCompress::reset_stream()
    {
        compress_stream_ = std::unique_ptr<z_stream, ZStreamDeflateDeleter>(new z_stream());
        compress_stream_->zalloc = Z_NULL;
        compress_stream_->zfree = Z_NULL;
        compress_stream_->opaque = Z_NULL;

        int32_t ret = deflateInit2(compress_stream_.get(), Z_BEST_COMPRESSION, Z_DEFLATED, MAX_WBITS, MAX_MEM_LEVEL,
                                   Z_DEFAULT_STRATEGY);
        if (ret != Z_OK)
        {
            compress_stream_.reset();
        }
    }

    void ZlibCompress::reset_uncompress_stream_()
    {
        uncompress_stream_ = std::unique_ptr<z_stream, ZStreamInflateDeleter>(new z_stream());
        uncompress_stream_->zalloc = Z_NULL;
        uncompress_stream_->zfree = Z_NULL;
        uncompress_stream_->opaque = Z_NULL;
        uncompress_stream_->avail_in = 0;
        uncompress_stream_->next_in = Z_NULL;
        int32_t ret = inflateInit2(uncompress_stream_.get(), MAX_WBITS);
        if (ret != Z_OK)
        {
            uncompress_stream_.reset();
        }
    }

    size_t ZlibCompress::compressed_bound(size_t input_size)
    {
        // 保守上界: deflate 对不可压缩数据的输出最多比原数据大约 0.03%
        // (deflateBound), 这里留 1/8 余量覆盖 zlib 头与同步标记开销;
        // 缓冲不足会在 compress 内触发 Z_BUF_ERROR 判失败(压缩中断)
        return input_size + input_size / 8 + 64;
    }

}