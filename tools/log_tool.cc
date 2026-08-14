#include "logger/rotater/CompressHandler.h"
#include "logger/rotater/EncryptHandler.h"
#include "utils/aes_crypt.h"
#include "utils/zlib_compress.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace
{

void print_usage(const char *prog)
{
    std::cerr << "Usage:\n"
              << "  " << prog << " compress   <file>              Compress file with zlib\n"
              << "  " << prog << " decompress <file>              Decompress file\n"
              << "  " << prog << " encrypt    <file> <key>        Encrypt file (AES-256-GCM)\n"
              << "  " << prog << " decrypt    <file> <key>        Decrypt file (AES-256-GCM)\n";
}

bool read_file(const std::string &path, std::vector<char> &data)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
    {
        std::cerr << "Error: cannot open " << path << "\n";
        return false;
    }
    data.resize(static_cast<size_t>(in.tellg()));
    in.seekg(0);
    in.read(data.data(), static_cast<std::streamsize>(data.size()));
    return true;
}

bool write_file(const std::string &path, const std::vector<char> &data)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        std::cerr << "Error: cannot write " << path << "\n";
        return false;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return true;
}

int cmd_compress(const std::string &filepath)
{
    // Read input
    std::vector<char> input;
    if (!read_file(filepath, input)) return 1;

    // Compress via CompressHandler (wraps utils::Compress)
    auto compressor = std::make_unique<utils::ZlibCompress>();
    // The ZlibCompress has a subtlety: reset_stream() must be called first
    compressor->reset_stream();

    size_t bound = compressor->compressed_bound(input.size());
    std::vector<char> output(bound);

    size_t out_len = compressor->compress(input.data(), input.size(), output.data(), bound);
    if (out_len == 0)
    {
        std::cerr << "Error: compression failed\n";
        return 1;
    }
    output.resize(out_len);

    std::string out_path = filepath + ".gz";
    if (!write_file(out_path, output)) return 1;

    std::cout << "Compressed " << filepath << " (" << input.size()
              << " -> " << out_len << " bytes) -> " << out_path << "\n";
    return 0;
}

int cmd_decompress(const std::string &filepath)
{
    std::vector<char> input;
    if (!read_file(filepath, input)) return 1;

    auto compressor = std::make_unique<utils::ZlibCompress>();
    compressor->reset_stream();

    std::string output = compressor->decompress(input.data(), input.size());
    if (output.empty())
    {
        std::cerr << "Error: decompression failed (not a gzip file?)\n";
        return 1;
    }

    // Strip .gz suffix
    std::string out_path = filepath;
    if (out_path.size() > 3 && out_path.substr(out_path.size() - 3) == ".gz")
    {
        out_path = out_path.substr(0, out_path.size() - 3);
    }
    else
    {
        out_path += ".dec";
    }

    std::vector<char> out_data(output.begin(), output.end());
    if (!write_file(out_path, out_data)) return 1;

    std::cout << "Decompressed " << filepath << " (" << input.size()
              << " -> " << output.size() << " bytes) -> " << out_path << "\n";
    return 0;
}

int cmd_encrypt(const std::string &filepath, const std::string &key)
{
    try
    {
        utils::AesCrypt crypt(key);
        std::string out_path = filepath + ".enc";
        logger::EncryptHandler::encrypt_file(filepath, out_path, crypt);
        std::cout << "Encrypted " << filepath << " -> " << out_path << " (AES-256-GCM)\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}

int cmd_decrypt(const std::string &filepath, const std::string &key)
{
    try
    {
        utils::AesCrypt crypt(key);
        std::string out_path = filepath;
        if (out_path.size() > 4 && out_path.substr(out_path.size() - 4) == ".enc")
            out_path = out_path.substr(0, out_path.size() - 4);
        else
            out_path += ".dec";

        logger::EncryptHandler::decrypt_file(filepath, out_path, crypt);
        std::cout << "Decrypted " << filepath << " -> " << out_path << " (AES-256-GCM)\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        print_usage(argv[0]);
        return 1;
    }

    std::string cmd = argv[1];
    std::string filepath = argv[2];

    if (cmd == "compress")
    {
        return cmd_compress(filepath);
    }
    if (cmd == "decompress")
    {
        return cmd_decompress(filepath);
    }
    if (cmd == "encrypt")
    {
        if (argc < 4)
        {
            std::cerr << "Error: encrypt requires a key\n";
            return 1;
        }
        return cmd_encrypt(filepath, argv[3]);
    }
    if (cmd == "decrypt")
    {
        if (argc < 4)
        {
            std::cerr << "Error: decrypt requires a key\n";
            return 1;
        }
        return cmd_decrypt(filepath, argv[3]);
    }

    print_usage(argv[0]);
    return 1;
}
