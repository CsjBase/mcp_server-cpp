#pragma once

#include <stddef.h>
#include <stdio.h>
#include <string>

namespace utils
{
    size_t thread_id() noexcept;

    bool fopen_s(FILE **fp, const std::string &filename, const std::string &mode);
    bool fsync(FILE *fp);
    bool fwrite_bytes(const void *ptr, const size_t bytes, FILE *fp);

    bool path_exists(const std::string &filename) noexcept;
    std::string dir_name(const std::string &path);
    bool mkdir(const std::string &path);
    bool remove_if_exists(const std::string &path) noexcept;

    void sleep_for_millis(unsigned int millis);
}