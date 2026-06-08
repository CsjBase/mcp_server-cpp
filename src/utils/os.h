#pragma once

#include <stddef.h>
#include <stdio.h>

namespace utils
{
    size_t thread_id() noexcept;

    bool fsync(FILE *fp);
    bool fwrite_bytes(const void *ptr, const size_t bytes, FILE *fp);
}