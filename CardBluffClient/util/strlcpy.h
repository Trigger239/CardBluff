#ifndef STRLCPY_H
#define STRLCPY_H

#include <cstddef>

std::size_t strlcpy(char *dst, const char *src, std::size_t dsize);
std::size_t wcslcpy(wchar_t *dst, const wchar_t *src, std::size_t dsize);

#endif // STRLCPY_H
