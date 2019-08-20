#include "util.h"

#include <algorithm>

#include "unicode.h"
const long long BASE = 10;
std::string ll_to_string(long long a)
{
	std::string ans;
	bool b = (a < 0);
	while (a)
	{
		ans.push_back('0' + static_cast<int>(abs(a % BASE)));
		a /= BASE;
	}
	if (b)
		ans.push_back('-');
	std::reverse(ans.begin(), ans.end());
	return ans;
}

std::wstring ll_to_wstring(long long a)
{
	return converter.from_bytes(ll_to_string(a));
}

std::wstring remove_spaces(const std::wstring& str)
{
    std::wstring ans;
    for (auto it = str.begin(); it != str.end(); ++it)
        if (*it != L' ')
            ans.push_back(*it);
    return ans;
}
