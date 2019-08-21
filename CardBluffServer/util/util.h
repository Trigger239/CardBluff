#ifndef UTIL_H_INCLUDED
#define UTIL_H_INCLUDED
#include <string>

std::string ll_to_string(long long a, bool plus_sign = false);
std::wstring ll_to_wstring(long long a, bool plus_sign = false);
std::wstring remove_space_characters(const std::wstring& str);

#endif // UTIL_H_INCLUDED
