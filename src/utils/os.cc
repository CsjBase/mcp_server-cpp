#include "utils/os.h"

#include <cerrno>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace utils
{
    size_t thread_id() noexcept
    {
        static thread_local const size_t tid = static_cast<size_t>(::syscall(SYS_gettid));
        return tid;
    }
    bool fopen_s(FILE **fp, const std::string &filename, const std::string &mode)
    {
        *fp = ::fopen(filename.c_str(), mode.c_str());
        return *fp != nullptr;
    }
    bool fsync(FILE *fp)
    {
        return ::fsync(fileno(fp)) == 0;
    }
    bool fwrite_bytes(const void *ptr, const size_t bytes, FILE *fp)
    {
        return ::fwrite_unlocked(ptr, 1, bytes, fp) == bytes;
    }

    bool path_exists(const std::string &filename) noexcept
    {
        struct stat st;
        return ::stat(filename.c_str(), &st) == 0;
    }
    std::string dir_name(const std::string &path)
    {
        if (path.empty())
            return ".";
        auto end = path.size() - 1;
        while (end > 0 && path[end] == '/')
            --end;
        auto pos = path.rfind('/', end);
        if (pos == std::string::npos)
            return ".";
        if (pos == 0)
            return "/";
        return path.substr(0, pos);
    }

    bool mkdir(const std::string &path)
    {
        if (path.empty())
        {
            return false;
        }
        if (path_exists(path))
        {
            return true;
        }

        size_t search_offset = 0;
        do
        {
            auto token_pos = path.find_first_of("/", search_offset);
            // treat the entire path as a folder if no folder separator not found
            if (token_pos == std::string::npos)
            {
                token_pos = path.size();
            }

            auto subdir = path.substr(0, token_pos);

            if (!subdir.empty() && !path_exists(subdir) && ::mkdir(subdir.c_str(), 0755) != 0)
            {
                return false; // return error if failed creating dir
            }
            search_offset = token_pos + 1;
        } while (search_offset < path.size());

        return true;
    }

    void sleep_for_millis(unsigned int millis)
    {
        ::usleep(millis * 1000);
    }
}