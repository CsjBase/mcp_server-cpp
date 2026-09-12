#include "net/http/utils.h"

namespace net
{
    namespace http
    {
        static const char xdigit_chars[256] = {
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,1,2,3,4,5,6,7,8,9,0,0,0,0,0,0,
            0,10,11,12,13,14,15,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,10,11,12,13,14,15,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        };

        std::string UrlDecode(const std::string &str, bool space_as_plus)
        {
            std::string *ss = nullptr;
            const char *end = str.c_str() + str.length();
            for (const char *c = str.c_str(); c < end; ++c)
            {
                if (*c == '+' && space_as_plus)
                {
                    if (!ss)
                    {
                        ss = new std::string;
                        ss->append(str.c_str(), c - str.c_str());
                    }
                    ss->append(1, ' ');
                }
                else if (*c == '%' && (c + 2) < end && isxdigit(*(c + 1)) && isxdigit(*(c + 2)))
                {
                    if (!ss)
                    {
                        ss = new std::string;
                        ss->append(str.c_str(), c - str.c_str());
                    }
                    ss->append(1, (char)(xdigit_chars[(int)*(c + 1)] << 4 | xdigit_chars[(int)*(c + 2)]));
                    c += 2;
                }
                else if (ss)
                {
                    ss->append(1, *c);
                }
            }
            if (!ss)
            {
                return str;
            }
            else
            {
                std::string rt = *ss;
                delete ss;
                return rt;
            }
        }

        std::string Time2Str(time_t ts, const std::string &format)
        {
            struct tm tm;
            localtime_r(&ts, &tm);
            char buf[64];
            strftime(buf, sizeof(buf), format.c_str(), &tm);
            return buf;
        }

        std::string Trim(const std::string& str, const std::string& delimit) {
            auto begin = str.find_first_not_of(delimit);
            if(begin == std::string::npos) {
                return "";
            }
            auto end = str.find_last_not_of(delimit);
            return str.substr(begin, end - begin + 1);
        }
    }
}