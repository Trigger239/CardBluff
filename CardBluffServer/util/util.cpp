#include "util.h"

#include <algorithm>
#include <sstream>

#include "unicode.h"
#include "date.h"

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

std::string ptr_to_string(void* ptr){
  char str[11]; //2 + 8 + 1
  sprintf(str, "0x%08X", (unsigned int) ptr);
  return std::string(str);
}

std::wstring ptr_to_wstring(void* ptr){
  return converter.from_bytes(ptr_to_string(ptr));
}

std::wstring time_to_wstring(std::chrono::high_resolution_clock::time_point t){
  auto tt = std::chrono::time_point_cast<std::chrono::milliseconds>(t);

  tt += std::chrono::hours(TIME_ZONE);

  std::stringstream ss;
  date::to_stream(ss, "%F %T", tt);

  return converter.from_bytes(ss.str());
}

std::wstring remove_space_characters(const std::wstring& str)
{
    std::wstring ans;
    for (auto it = str.begin(); it != str.end(); ++it)
        if ((*it != L' ') && (*it != L'\t'))
            ans.push_back(*it);
    return ans;
}
