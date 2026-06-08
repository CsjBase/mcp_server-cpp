#include "utils/os.h"

#include <unistd.h>
#include <sys/syscall.h>

namespace utils
{
    size_t thread_id() noexcept
    {
        static thread_local const size_t tid = static_cast<size_t>(::syscall(SYS_gettid));
        return tid;
    }

    bool fsync(FILE *fp)
    {
        return ::fsync(fileno(fp)) == 0;
    }
    bool fwrite_bytes(const void *ptr, const size_t bytes, FILE *fp)
    {
        return ::fwrite_unlocked(ptr, 1, bytes, fp) == bytes;
    }
}