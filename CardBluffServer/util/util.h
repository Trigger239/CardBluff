#ifndef UTIL_H_INCLUDED
#define UTIL_H_INCLUDED
#include <string>
#include <chrono>

#define TIME_ZONE +3

std::string ll_to_string(long long a, bool plus_sign = false);
std::wstring ll_to_wstring(long long a, bool plus_sign = false);
std::string ptr_to_string(void* ptr);
std::wstring ptr_to_wstring(void* ptr);
std::wstring time_to_wstring(std::chrono::high_resolution_clock::time_point t = std::chrono::high_resolution_clock::now());
std::wstring remove_space_characters(const std::wstring& str);

#endif // UTIL_H_INCLUDED
