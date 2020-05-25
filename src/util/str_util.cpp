#include "str_util.hpp"
#include <algorithm>
#include <cctype>
#include <locale>

namespace horizon {
void ltrim(std::string &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) { return !std::isspace(ch); }));
}

// trim from end (in place)
void rtrim(std::string &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(), s.end());
}

// trim from both ends (in place)
void trim(std::string &s)
{
    ltrim(s);
    rtrim(s);
}

// trim from start (copying)
std::string ltrim_copy(std::string s)
{
    ltrim(s);
    return s;
}

// trim from end (copying)
std::string rtrim_copy(std::string s)
{
    rtrim(s);
    return s;
}

bool has_suffix(std::string s, std::string suffix)
{
    if (suffix.size() > s.size())
        return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}
} // namespace horizon
