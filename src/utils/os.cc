#include "os.h"

#include <unistd.h>
#include <sys/syscall.h>

namespace utils
{
    namespace os
    {
        size_t thread_id() noexcept
        {
            static thread_local const size_t tid = static_cast<size_t>(::syscall(SYS_gettid));
            return tid;
        }
    }
}