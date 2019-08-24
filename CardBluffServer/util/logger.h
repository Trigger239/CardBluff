#ifndef LOGGER_H_INCLUDED
#define LOGGER_H_INCLUDED

#include <windows.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cwchar>

#define LOG_WITH_TIME

//class Logger::EndLine{};

class Logger{
public:
  class EndLine{};

  Logger(const std::string& logfile_name, HANDLE console, HANDLE _mutex);
  Logger(const char* logfile_name, HANDLE console, HANDLE _mutex);
  Logger(const std::wstring& prefix);
  Logger(const wchar_t* prefix);

  void set_prefix(const std::wstring& _prefix);
  void set_prefix(const wchar_t* _prefix);

  void log(const std::wstring& message);

  void operator() (const std::wstring& message);
  Logger& operator<< (EndLine);
  Logger& operator<< (const std::wstring& message);
  Logger& operator<< (const wchar_t* message);
  Logger& operator<< (const std::string& message);
  Logger& operator<< (const char* message);
  Logger& operator<< (long long n);
  Logger& operator<< (void* ptr);

  static EndLine endline;

private:
  static HANDLE mutex;
  static std::ofstream logfile;
  static HANDLE log_console;

  std::wstring prefix;
  std::wstringstream line_buffer;

  void write_line(const std::wstring& str);
  std::wstring get_formatted_time();
};

#endif // LOGGER_H_INCLUDED
