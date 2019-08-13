#ifndef UNICODE_H_INCLUDED
#define UNICODE_H_INCLUDED

#include <locale>
#include <codecvt>
#include <cwchar>

extern std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;

#endif // UNICODE_H_INCLUDED
