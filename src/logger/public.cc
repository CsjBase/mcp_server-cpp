#include "public.h"
#include "utils/os.h"

#include <algorithm>
#include <sys/stat.h>

namespace logger
{
    constexpr static std::string_view level_string_views[] LOG_LEVEL_NAMES;

    const std::string_view &to_string_view(const LogLevel &l)
    {
        return level_string_views[static_cast<int>(l)];
    }
    LogLevel from_str(const std::string &name)
    {
        auto it = std::find_if(std::begin(level_string_views), std::end(level_string_views),
                               [&name](const std::string_view &level_name)
                               {
                                   return level_name.size() == name.size() &&
                                          std::equal(name.begin(), name.end(), level_name.begin(),
                                                     [](char a, char b)
                                                     {
                                                         return std::tolower(static_cast<unsigned char>(a)) ==
                                                                std::tolower(static_cast<unsigned char>(b));
                                                     });
                               });
        if (it != std::end(level_string_views))
            return static_cast<LogLevel>(std::distance(std::begin(level_string_views), it));

        auto iequals = [](const std::string &a, const std::string &b)
        {
            return a.size() == b.size() &&
                   std::equal(a.begin(), a.end(), b.begin(), [](char ac, char bc)
                              { return std::tolower(static_cast<unsigned char>(ac)) ==
                                       std::tolower(static_cast<unsigned char>(bc)); });
        };

        if (iequals(name, "warn"))
        {
            return LogLevel::Warn;
        }
        if (iequals(name, "err"))
        {
            return LogLevel::Error;
        }
        return LogLevel::Off;
    }

    LogException::LogException(std::string msg)
        : msg_(std::move(msg))
    {
    }
    LogException::LogException(std::string msg, int last_errno)
    {
        memory_buf_t outbuf;
        fmt::format_system_error(outbuf, last_errno, msg.c_str());
        msg_ = fmt::to_string(outbuf);
    }

    const char *LogException::what() const noexcept
    {
        return msg_.c_str();
    }

    FileHelper::~FileHelper()
    {
        close();
    }
    void FileHelper::open(const std::string &fname, bool truncate)
    {
        close();
        filename_ = fname;

        const char *mode = "ab";
        const char *trunc_mode = "wb";
        for (int tries = 0; tries < open_tries_; ++tries)
        {
            // create containing folder if not exists already.
            utils::mkdir(utils::dir_name(fname));
            if (truncate)
            {
                // Truncate by opening-and-closing a tmp file in "wb" mode, always
                // opening the actual log-we-write-to in "ab" mode, since that
                // interacts more politely with eternal processes that might
                // rotate/truncate the file underneath us.
                std::FILE *tmp;
                if (!utils::fopen_s(&tmp, fname, trunc_mode))
                {
                    continue;
                }
                std::fclose(tmp);
            }
            if (utils::fopen_s(&fd_, fname, mode))
            {
                return;
            }

            utils::sleep_for_millis(open_interval_);
        }
        throw LogException("Failed to open log file: " + fname, errno);
    }
    void FileHelper::reopen(bool truncate)
    {
        if (filename_.empty())
        {
            throw LogException("Can't reopen log file: no filename specified");
        }
        open(filename_, truncate);
    }
    void FileHelper::flush()
    {
        if (std::fflush(fd_) != 0)
        {
            throw LogException("Failed to flush log file: " + filename_, errno);
        }
    }
    void FileHelper::sync()
    {
        if (utils::fsync(fd_) != 0)
        {
            throw LogException("Failed to sync log file: " + filename_, errno);
        }
    }
    void FileHelper::close()
    {
        if (fd_ != nullptr)
        {
            std::fclose(fd_);
            fd_ = nullptr;
        }
    }
    void FileHelper::write(const memory_buf_t &buf)
    {
        if (fd_ != nullptr)
        {
            if (!utils::fwrite_bytes(buf.data(), buf.size(), fd_))
            {
                throw LogException("Failed to write to log file: " + filename_, errno);
            }
        }
    }
    size_t FileHelper::size() const
    {
        if (fd_ == nullptr)
        {
            throw LogException("Can't get size of log file: no file opened");
        }
        struct stat64 st;
        if (::stat64(filename_.c_str(), &st) == 0)
        {
            return static_cast<size_t>(st.st_size);
        }
        throw LogException("Failed to get size of log file: " + filename_, errno);
        return 0;
    }
    const std::string &FileHelper::filename() const
    {
        return filename_;
    }

    std::tuple<std::string, std::string> FileHelper::split_by_extension(const std::string &fname)
    {
        auto ext_index = fname.rfind('.');

        if (ext_index == std::string::npos || ext_index == 0 || ext_index == fname.size() - 1)
        {
            return std::make_tuple(fname, std::string());
        }

        // treat cases like "/etc/rc.d/somelogfile or "/abc/.hiddenfile"
        auto folder_index = fname.find_last_of("/");
        if (folder_index != std::string::npos && folder_index >= ext_index - 1)
        {
            return std::make_tuple(fname, std::string());
        }

        // finally - return a valid base and extension tuple
        return std::make_tuple(fname.substr(0, ext_index), fname.substr(ext_index));
    }
}
