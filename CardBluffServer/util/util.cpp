#include "util.h"

#include <algorithm>

#include "unicode.h"
const long long BASE = 10;
std::string ll_to_string(long long a, bool plus_sign)
{
  if(a == 0)
    return std::string("0");
	std::string ans;
	bool b = (a < 0);
	while (a)
	{
		ans.push_back('0' + static_cast<int>(abs(a % BASE)));
		a /= BASE;
	}
	if (b)
		ans.push_back('-');
  else if (plus_sign)
    ans.push_back('+');
	std::reverse(ans.begin(), ans.end());
	return ans;
}

std::wstring ll_to_wstring(long long a, bool plus_sign)
{
	return converter.from_bytes(ll_to_string(a, plus_sign));
}

std::wstring remove_space_characters(const std::wstring& str)
{
    std::wstring ans;
    for (auto it = str.begin(); it != str.end(); ++it)
        if ((*it != L' ') && (*it != L'\t'))
            ans.push_back(*it);
    return ans;
}
