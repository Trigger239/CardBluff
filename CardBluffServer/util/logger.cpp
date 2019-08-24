#include "logger.h"

#include <chrono>
#include <ctime>

#include "util.h"
#include "unicode.h"
#include "strlcpy.h"

HANDLE Logger::mutex;
std::ofstream Logger::logfile;
HANDLE Logger::log_console;
Logger::EndLine endline;

Logger::Logger(const std::string& logfile_name, HANDLE console, HANDLE _mutex){
  if(mutex)
    return;
  mutex = _mutex;
  logfile.open(logfile_name);
  log_console = console;
}

Logger::Logger(const char* logfile_name, HANDLE console, HANDLE _mutex){
  Logger(std::string(logfile_name), console, _mutex);
}

Logger::Logger(const std::wstring& prefix)
  : prefix(prefix){};

Logger::Logger(const wchar_t* prefix)
  : prefix(prefix){};

void Logger::set_prefix(const std::wstring& _prefix){
  prefix = _prefix;
}

void Logger::set_prefix(const wchar_t* _prefix){
  prefix = std::wstring(_prefix);
}

void Logger::log(const std::wstring& message){
#ifdef LOG_WITH_TIME
  write_line(time_to_wstring() + L" " + prefix + L": " + message);
#else
  write_line(prefix + L": " + message);
#endif
}

void Logger::operator() (const std::wstring& message){
  log(message);
}

Logger& Logger::operator<< (Logger::EndLine el){
  log(line_buffer.str());
  line_buffer.str(std::wstring());
  line_buffer.clear();
  return *this;
}

Logger& Logger::operator<< (const std::wstring& message){
  line_buffer << message;
  return *this;
}

Logger& Logger::operator<< (const wchar_t* message){
  return operator<<(std::wstring(message));
}

Logger& Logger::operator<< (const std::string& message){
  return operator<<(converter.from_bytes(message));
}

Logger& Logger::operator<< (const char* message){
  return operator<<(converter.from_bytes(message));
}

Logger& Logger::operator<< (long long n){
  return operator<<(ll_to_wstring(n));
}

Logger& Logger::operator<< (void* ptr){
  return operator<<(L"0x" + ptr_to_wstring(ptr));
}

void Logger::write_line(const std::wstring& str){
  DWORD written;
  std::string narrow_str = converter.to_bytes(str);
  WaitForSingleObject(mutex, INFINITE);
  logfile << narrow_str << '\n';
  logfile.flush();
  WriteConsoleW(log_console, str.c_str(), str.size(), &written, NULL);
  WriteConsoleW(log_console, L"\n", 1, &written, NULL);
  ReleaseMutex(mutex);
}
