#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <winsock.h>
#include <iostream>
#include <conio.h>
#include <signal.h>
#include <cstdio>
#include <functional>
#include <cctype>
#include "sha256/sha256.h"
#include "util/strlcpy.h"
#include "util/unicode.h"
//#include "util/lambda_to_func.h"

#undef MOUSE_MOVED
#define PDC_WIDE
#define NCURSES_WIDECHAR 1
#include "pdcurses/curses.h"

typedef struct _CONSOLE_FONT_INFOEX
{
    ULONG cbSize;
    DWORD nFont;
    COORD dwFontSize;
    UINT  FontFamily;
    UINT  FontWeight;
    WCHAR FaceName[LF_FACESIZE];
}CONSOLE_FONT_INFOEX, *PCONSOLE_FONT_INFOEX;
//the function declaration begins
#ifdef __cplusplus
extern "C" {
#endif
BOOL WINAPI SetCurrentConsoleFontEx(HANDLE hConsoleOutput, BOOL bMaximumWindow, PCONSOLE_FONT_INFOEX
lpConsoleCurrentFontEx);
BOOL WINAPI GetCurrentConsoleFontEx(HANDLE hConsoleOutput, BOOL bMaximumWindow, PCONSOLE_FONT_INFOEX
lpConsoleCurrentFontEx);
#ifdef __cplusplus
}
#endif

using namespace std;

//DECLARATIONS
//error trapping signals
#define SIGINT 2
#define SIGKILL 9
#define SIGQUIT 3

#define RECEIVE_BUFFER_SIZE 256
#define SEND_BUFFER_SIZE 256

#define PORT 2390

enum state_t{
  ENTER_NICKNAME,
  NICKNAME_SENT,
  ENTER_PASSWORD_REGISTER_FIRST,
  PASSWORD_REGISTER_FIRST_SENT,
  ENTER_PASSWORD_REGISTER_SECOND,
  PASSWORD_REGISTER_SECOND_SENT,
  ENTER_PASSWORD,
  PASSWORD_SENT,
  AUTHORIZED,
  AUTHORIZATION_FAILED
} state;
unsigned long long salt;

// SOCKETS
SOCKET sock, client;

HANDLE stdin_mutex;
HANDLE stdout_mutex;
HANDLE socket_mutex;

void printf_mt(const char* format, ...){
  va_list args;
  va_start(args, format);
  WaitForSingleObject(stdout_mutex, INFINITE);
  vprintf(format, args);
  ReleaseMutex(stdout_mutex);
  va_end(args);
}

void wprintf_mt(const wchar_t* format, ...){
  va_list args;
  va_start(args, format);
  WaitForSingleObject(stdout_mutex, INFINITE);
  vwprintf(format, args);
  ReleaseMutex(stdout_mutex);
  va_end(args);
}

//if c == 0, then this function checks for any char in the buffer
//if c != 0, it checks for c in the buffer
//returns true on success, false on error
bool console_has_char_in_input_buffer(HANDLE stdin_handle,
                                      bool* result, wchar_t c = 0){
  DWORD number_of_input_events;
  INPUT_RECORD events[1024];

  if(!PeekConsoleInputW(stdin_handle, events, 1024, &number_of_input_events)){
    return false;
  };
  for(unsigned int i = 0; i < number_of_input_events; i++){
    if(events[i].EventType == KEY_EVENT){
      if(events[i].Event.KeyEvent.bKeyDown == true &&
        (c == 0 || events[i].Event.KeyEvent.uChar.UnicodeChar == c)){
        *result = true;
        return true;
      }
    }
  }
  *result = false;
  return true;
}

void scanf_mt(const char* format, ...){
  va_list args;
  va_start(args, format);
  WaitForSingleObject(stdin_mutex, INFINITE);
  vscanf(format, args);
  ReleaseMutex(stdin_mutex);
  va_end(args);
}

void wscanf_mt(const wchar_t* format, ...){
  va_list args;
  va_start(args, format);
  WaitForSingleObject(stdin_mutex, INFINITE);
  vwscanf(format, args);
  ReleaseMutex(stdin_mutex);
  va_end(args);
}

//returns true on success, false on error
bool cin_getline_mt(wchar_t* s, unsigned int n,
                    HANDLE stdin_handle = INVALID_HANDLE_VALUE,
                    HANDLE stdout_handle = INVALID_HANDLE_VALUE,
                    HANDLE terminate_event = INVALID_HANDLE_VALUE,
                    bool* _terminate = nullptr){
  if(stdout_handle == INVALID_HANDLE_VALUE ||
     stdin_handle == INVALID_HANDLE_VALUE ||
     terminate_event == INVALID_HANDLE_VALUE ||
     _terminate == nullptr){
    WaitForSingleObject(stdin_mutex, INFINITE);
    wcin.getline(s, n);
    ReleaseMutex(stdin_mutex);
    return true;
  }

  *_terminate = false;
  HANDLE handles[2] = {stdin_mutex, terminate_event};
  DWORD wait_result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
  if(wait_result == WAIT_OBJECT_0 + 1){
    *_terminate = true;
    return true;
  }
  if(wait_result != WAIT_OBJECT_0 + 0){
    return false;
  }

  wstring buf;
  string buf_raw;

  while(true){
    bool char_available;
    DWORD count;

    if(!console_has_char_in_input_buffer(stdin_handle, &char_available)){
      ReleaseMutex(stdin_mutex);
      return false;
    }

    if(WaitForSingleObject(terminate_event, 0) == WAIT_OBJECT_0){
      ReleaseMutex(stdin_mutex);
      *_terminate = true;
      return true;
    }

    if(char_available){

      //wcin.getline(s, n);
      getline(cin, buf_raw);
      //buf = converter.from_bytes(buf_raw);
      break;
    }
    else
      Sleep(10);
  }

  ReleaseMutex(stdin_mutex);

  wcslcpy(s, buf.c_str(), n);
  return true;
}

//returns true on success, false on error
bool get_password(HANDLE stdin_handle,
                  HANDLE stdout_handle,
                  wstring* result,
                  HANDLE terminate_event = INVALID_HANDLE_VALUE,
                  bool* _terminate = nullptr){
  // Set the console mode to no-echo, not-line-buffered input
  DWORD mode;
  if(!GetConsoleMode(stdin_handle, &mode))
    return false;
  if(!SetConsoleMode(stdin_handle, mode & ~(ENABLE_ECHO_INPUT |
                                            ENABLE_LINE_INPUT |
                                            ENABLE_PROCESSED_INPUT)))
    return false;

  wchar_t c;
  bool char_available;
  wstring res;
  DWORD count;

  if(_terminate != nullptr)
    *_terminate = false;

  while(true){
    if(!console_has_char_in_input_buffer(stdin_handle, &char_available)){
      WriteConsoleW(stdout_handle, L"\n", 1, &count, NULL);
      SetConsoleMode(stdin_handle, mode);
      return false;
    }

    if(terminate_event != INVALID_HANDLE_VALUE && _terminate != nullptr)
      if(WaitForSingleObject(terminate_event, 0) == WAIT_OBJECT_0){
        WriteConsoleW(stdout_handle, L"\n", 1, &count, NULL);
        SetConsoleMode(stdin_handle, mode);
        *_terminate = true;
        return true;
      }

    if(char_available){
      if(!ReadConsoleW(stdin_handle, &c, 1, &count, NULL)){
        WriteConsoleW(stdout_handle, L"\n", 1, &count, NULL);
        SetConsoleMode(stdin_handle, mode);
        return false;
      }
      if(count != 0){
        if((c == L'\r') || (c == L'\n'))
          break;
        if(c == L'\b'){
          if(res.length()){
            if(!WriteConsoleW(stdout_handle, L"\b \b", 3, &count, NULL)){
              WriteConsoleW(stdout_handle, L"\n", 1, &count, NULL);
              SetConsoleMode(stdin_handle, mode);
              return false;
            }
            res.pop_back();
          }
        }
        else{
          if(!WriteConsoleW(stdout_handle, L"*", 1, &count, NULL)){
            WriteConsoleW(stdout_handle, L"\n", 1, &count, NULL);
            SetConsoleMode(stdin_handle, mode);
            return false;
          }
          res.push_back(c);
        }
      }
    }
    else
      Sleep(10);
  }

  WriteConsoleW(stdout_handle, L"\n", 1, &count, NULL);
  SetConsoleMode(stdin_handle, mode);

  *result = res;

  return true;
}

void s_handle(int s)
{
  if(sock)
     closesocket(sock);
  if(client)
     closesocket(client);
  WSACleanup();
  Sleep(1000);
  cout << "EXIT SIGNAL :" << s;
  exit(0);
}


void s_cl(const char *a, int x)
{
  cout << a;
  s_handle(x + 1000);
}

struct thread_params_t{
  HANDLE send_thread;
  HANDLE receive_thread;
  HANDLE stdout_handle;
  HANDLE stdin_handle;
  SOCKET socket;
  HANDLE thread_handles_ready_event;
  HANDLE terminate_event;
  HANDLE nickname_request_event;
  HANDLE pass_reg_first_request_event;
  HANDLE pass_reg_second_request_event;
  HANDLE password_request_event;
  HANDLE authorized_event;
  HANDLE auth_fail_event;
  WINDOW* input_win;
  WINDOW* output_win;
};

HANDLE curses_mutex;

int use_win(WINDOW *win, std::function<int(WINDOW*)> cb_func){
//  auto callback = [=](WINDOW* w){
//    return cb_func(w);
//  };
//  auto thunk = [](WINDOW* w, void* arg){ // note thunk is captureless
//    return (*static_cast<decltype(callback)*>(arg))(w);
//  };

  int ret;
  WaitForSingleObject(curses_mutex, INFINITE);
  //ret = use_window(win, thunk, &callback);
  ret = cb_func(win);
  ReleaseMutex(curses_mutex);
  return ret;
}

int win_addwstr(WINDOW* win, const wchar_t* str){
  return use_win(win, [&](WINDOW* w){int res = waddwstr(w, str); wrefresh(w); return res;});
}

int win_wprintw(WINDOW* win, const char* format, ...){
  int ret;
  va_list args;
  va_start(args, format);
  ret = use_win(win, [&](WINDOW* w){int res = vwprintw(w, (const char*) format, args); wrefresh(w); return res;});
  va_end(args);
  return ret;
}

bool win_get_wstr(WINDOW* input_win, WINDOW* output_win,
                  wstring &str,
                  HANDLE terminate_event,
                  bool* _terminate = nullptr){
  *_terminate = false;

  wstring input_buffer;
  int cursor = 0;
  bool string_ready = false;
  bool need_update = false;
  bool ret = true;

  while(true){
    if(WaitForSingleObject(terminate_event, 0) == WAIT_OBJECT_0){
      *_terminate = true;
      break;
    }

    wchar_t c;
    //int res = getch((wint_t*) &c);
    c = getch();
    //if(res == ERR)
    if(c == (wchar_t) ERR)
      continue;

    need_update = true;

    //if((res != KEY_CODE_YES) && iswprint(c)) {
    if(!(c & KEY_CODE_YES) && iswprint(c)) {
        input_buffer.insert(cursor++, 1, c);
    }

    switch(c){
    case ERR: /* no key pressed */ break;
    case KEY_LEFT:
      if(cursor > 0)
        cursor--;
      break;

    case KEY_RIGHT:
      if(cursor < input_buffer.size())
        cursor++;
      break;

    case KEY_HOME:
      cursor = 0;
      break;

    case KEY_END:
      cursor = input_buffer.size();
      break;

    case L'\t':
      break;

    case KEY_BACKSPACE:
    case 127:
    case 8:
      if(cursor <= 0){
        break;
      }
      cursor--;
      // Fall-through
    case KEY_DC:
      if(cursor < input_buffer.size()){
        input_buffer.erase(cursor);
      }
      break;

    case KEY_ENTER:
    case L'\r':
    case L'\n':
      str = input_buffer;
      string_ready = true;
      break;
    }

    if(!string_ready && need_update){
      if(use_win(input_win, [&](WINDOW* w){
          if(wclear(w) == ERR) return ERR;
          if(box(w, 0, 0) == ERR) return ERR;
          if(mvwaddwstr(w, 1, 1, input_buffer.c_str()) == ERR) return ERR;
          if(mvwchgat(w, 1, cursor + 1, 1, A_UNDERLINE, 2, nullptr) == ERR) return ERR;
          return wrefresh(w);
        }) == ERR)
      {
        ret = false;
        break;
      }
      need_update = false;
    }

    if(string_ready){
      if(use_win(input_win, [&](WINDOW* w){
          if(wclear(w) == ERR) return ERR;
          if(box(w, 0, 0) == ERR) return ERR;
          return wrefresh(w);
        }) == ERR)
      {
        ret = false;
        break;
      }
      input_buffer += L"\n";
      if(win_addwstr(output_win, input_buffer.c_str()) == ERR)
        ret = false;
      break;
    }
  }

  if(*_terminate){
    return true;
  }

  return ret;
}

DWORD WINAPI client_to_server(LPVOID lpParam){
  thread_params_t* params = (thread_params_t*) lpParam;
  SOCKET socket = params->socket;
  HANDLE stdout_handle = params->stdout_handle;
  HANDLE stdin_handle = params->stdin_handle;
  HANDLE thread_handles_ready_event = params->terminate_event;
  HANDLE terminate_event = params->terminate_event;
  HANDLE nickname_request_event = params->nickname_request_event;
  HANDLE pass_reg_first_request_event = params->pass_reg_first_request_event;
  HANDLE pass_reg_second_request_event = params->pass_reg_second_request_event;
  HANDLE password_request_event = params->password_request_event;
  HANDLE authorized_event = params->authorized_event;
  HANDLE auth_fail_event = params->auth_fail_event;
  WINDOW* input_win = params->input_win;
  WINDOW* output_win = params->output_win;

  HANDLE events[] = {
    terminate_event,
    nickname_request_event,
    pass_reg_first_request_event,
    pass_reg_second_request_event,
    password_request_event,
    authorized_event,
    auth_fail_event
  };

  wstring input_buffer;

  wchar_t send_buffer[SEND_BUFFER_SIZE];
  int res;
  bool should_send;
  wchar_t salt_str[17];

  bool _terminate = false;
  wstring pass;

  win_addwstr(output_win, L"Send thread started.\n");

  while(true){
    should_send = true;
    switch(WaitForMultipleObjects(sizeof(events) / sizeof(HANDLE),
                                  events, FALSE, INFINITE)){
    case WAIT_OBJECT_0 + 0: //terminate
      win_addwstr(output_win, L"Send thread terminated.\n");
      return 0;
      break;

    case WAIT_OBJECT_0 + 1:
      if(!win_get_wstr(input_win, output_win, input_buffer,
                       terminate_event, &_terminate) || _terminate){
        if(!_terminate)
          win_addwstr(output_win, L"Error reading nickname from console.\n");
        if(WaitForSingleObject(terminate_event, 0) != WAIT_OBJECT_0){
          SetEvent(terminate_event);
          WaitForSingleObject(thread_handles_ready_event, INFINITE);
          WaitForSingleObject(params->receive_thread, INFINITE);
        }
        win_addwstr(output_win, L"Send thread terminated.\n");
        return 0;
      }
      input_buffer = L"nickname:" + input_buffer;
      wcslcpy(send_buffer, input_buffer.c_str(), SEND_BUFFER_SIZE);
      ResetEvent(nickname_request_event);
      break;

    case WAIT_OBJECT_0 + 2:
      if(!get_password(stdin_handle, stdout_handle, &pass,
                       terminate_event, &_terminate) || _terminate){
        if(!_terminate)
          win_addwstr(output_win, L"Error reading password from console.\n");
        if(WaitForSingleObject(terminate_event, 0) != WAIT_OBJECT_0){
          SetEvent(terminate_event);
          WaitForSingleObject(thread_handles_ready_event, INFINITE);
          WaitForSingleObject(params->receive_thread, INFINITE);
        }
        win_addwstr(output_win, L"Send thread terminated.\n");
        return 0;
      }
      wcscpy(send_buffer, L"password_first:");
      wcscat(send_buffer, converter.from_bytes(sha256(converter.to_bytes(pass))).c_str());
      ResetEvent(pass_reg_first_request_event);
      break;

    case WAIT_OBJECT_0 + 3:
      if(!get_password(stdin_handle, stdout_handle, &pass,
                       terminate_event, &_terminate) || _terminate){
        if(!_terminate)
          win_addwstr(output_win, L"Error reading password from console.\n");
        if(WaitForSingleObject(terminate_event, 0) != WAIT_OBJECT_0){
          SetEvent(terminate_event);
          WaitForSingleObject(thread_handles_ready_event, INFINITE);
          WaitForSingleObject(params->receive_thread, INFINITE);
        }
        win_addwstr(output_win, L"Send thread terminated.\n");
        return 0;
      }
      swprintf(salt_str, L"%08x%08x",
              (unsigned int) (salt >> 32),
              (unsigned int) (salt & 0xFFFFFFFF));
      pass = converter.from_bytes(sha256(converter.to_bytes(pass)));
      pass += wstring(salt_str);
      pass = converter.from_bytes(sha256(converter.to_bytes(pass)));

      pass = wstring(L"password_second:") + pass;

      wcscpy(send_buffer, pass.c_str());
      ResetEvent(pass_reg_second_request_event);
      break;

    case WAIT_OBJECT_0 + 4:
      if(!win_get_wstr(input_win, output_win, pass,
                       terminate_event, &_terminate) || _terminate){
        if(!_terminate)
          win_addwstr(output_win, L"Error reading password from console.\n");
        if(WaitForSingleObject(terminate_event, 0) != WAIT_OBJECT_0){
          SetEvent(terminate_event);
          WaitForSingleObject(thread_handles_ready_event, INFINITE);
          WaitForSingleObject(params->receive_thread, INFINITE);
        }
        win_addwstr(output_win, L"Send thread terminated.\n");
        return 0;
      }
      swprintf(salt_str, L"%08x%08x",
              (unsigned int) (salt >> 32),
              (unsigned int) (salt & 0xFFFFFFFF));
      pass = converter.from_bytes(sha256(converter.to_bytes(pass)));
      pass += wstring(salt_str);
      pass = converter.from_bytes(sha256(converter.to_bytes(pass)));

      pass = wstring(L"password:") + pass;

      wcscpy(send_buffer, pass.c_str());
      ResetEvent(password_request_event);
      break;

    case WAIT_OBJECT_0 + 5: //authorized
      if(!win_get_wstr(input_win, output_win, input_buffer,
                        terminate_event, &_terminate) || _terminate){
        if(!_terminate)
          win_addwstr(output_win, L"Error reading from console.");
        if(WaitForSingleObject(terminate_event, 0) != WAIT_OBJECT_0){
          SetEvent(terminate_event);
          WaitForSingleObject(thread_handles_ready_event, INFINITE);
          WaitForSingleObject(params->receive_thread, INFINITE);
        }
        win_addwstr(output_win, L"Send thread terminated.\n");
        return 0;
      }
      wcslcpy(send_buffer, input_buffer.c_str(), SEND_BUFFER_SIZE);
      break;

    default:
      Sleep(10);
      should_send = false;
      break;
    }

    if(should_send){
      char send_buffer_raw[SEND_BUFFER_SIZE];
      strlcpy(send_buffer_raw, converter.to_bytes(send_buffer).c_str(), SEND_BUFFER_SIZE);
      while(send(socket, send_buffer_raw, SEND_BUFFER_SIZE, 0) == SOCKET_ERROR){
        if(WSAGetLastError() != WSAEWOULDBLOCK){
          win_addwstr(output_win, L"Connection error. Finishing all threads...\n");
          if(WaitForSingleObject(terminate_event, 0) != WAIT_OBJECT_0){
            SetEvent(terminate_event);
            WaitForSingleObject(thread_handles_ready_event, INFINITE);
            WaitForSingleObject(params->receive_thread, INFINITE);
          }
          win_addwstr(output_win, L"Send thread terminated.\n");
          return 0;
        }
        if(WaitForSingleObject(terminate_event, 0) == WAIT_OBJECT_0){
          win_addwstr(output_win, L"Send thread terminated.\n");
          return 0;
        }
        Sleep(10);
      }
    }
  }
}

DWORD WINAPI server_to_client(LPVOID lpParam){
  thread_params_t* params = (thread_params_t*) lpParam;
  SOCKET socket = params->socket;
  HANDLE thread_handles_ready_event = params->terminate_event;
  HANDLE terminate_event = params->terminate_event;
  HANDLE nickname_request_event = params->nickname_request_event;
  HANDLE pass_reg_first_request_event = params->pass_reg_first_request_event;
  HANDLE pass_reg_second_request_event = params->pass_reg_second_request_event;
  HANDLE password_request_event = params->password_request_event;
  HANDLE authorized_event = params->authorized_event;
  HANDLE auth_fail_event = params->auth_fail_event;
  WINDOW* input_win = params->input_win;
  WINDOW* output_win = params->output_win;

  char receive_buffer_raw[RECEIVE_BUFFER_SIZE];
  wstring receive_buffer;
  int res;
  win_addwstr(output_win, L"Receive thread started.\n");
  while(true){
    while(recv(socket, receive_buffer_raw, RECEIVE_BUFFER_SIZE, 0) == SOCKET_ERROR){
      if(WSAGetLastError() != WSAEWOULDBLOCK){
        win_addwstr(output_win, L"Connection error. Finishing all threads...\n");
        if(WaitForSingleObject(terminate_event, 0) != WAIT_OBJECT_0){
          SetEvent(terminate_event);
          WaitForSingleObject(thread_handles_ready_event, INFINITE);
          WaitForSingleObject(params->send_thread, INFINITE);
        }
        win_addwstr(output_win, L"Receive thread terminated.\n");
        return 0;
      }
      if(WaitForSingleObject(terminate_event, 0) == WAIT_OBJECT_0){
        win_addwstr(output_win, L"Receive thread terminated.\n");
        return 0;
      }
      Sleep(10);
    }

    receive_buffer = converter.from_bytes(receive_buffer_raw);

    if(wcscmp(receive_buffer.c_str(), L"") != 0){
      if(WaitForSingleObject(authorized_event, 0) == WAIT_OBJECT_0){
        win_addwstr(output_win, (L"> " + receive_buffer + L"\n").c_str());
      }
      else{
        if(wcscmp(receive_buffer.c_str(), L"password_first?") == 0){
          SetEvent(pass_reg_first_request_event);
        }
        else if(wcsncmp(receive_buffer.c_str(), L"password_second?", 16) == 0){
          unsigned int salt_h, salt_l;
          swscanf(receive_buffer.c_str() + 16, L"%8x%8x", &salt_h, &salt_l);
          salt = (((unsigned long long) salt_h) << 32) | salt_l;
          SetEvent(pass_reg_second_request_event);
        }
        else if(wcsncmp(receive_buffer.c_str(), L"password?", 9) == 0){
          unsigned int salt_h, salt_l;
          swscanf(receive_buffer.c_str() + 9, L"%8x%8x", &salt_h, &salt_l);
          salt = (((unsigned long long) salt_h) << 32) | salt_l;
          SetEvent(password_request_event);
        }
        else if(wcscmp(receive_buffer.c_str(), L"auth_ok!") == 0){
          SetEvent(authorized_event);
        }
        else if(wcscmp(receive_buffer.c_str(), L"auth_fail!") == 0){
          SetEvent(auth_fail_event);
        }
        else
          win_addwstr(output_win, (L"> " + receive_buffer + L"\n").c_str());
          //win_wprintw(output_win, "> %s\n", receive_buffer.c_str());
      }
    }
  }
}

#define INPUT_WINDOW_ROWS 3

void set_console_font(const wchar_t* font)
{
    HANDLE StdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_FONT_INFOEX info;
    memset(&info, 0, sizeof(CONSOLE_FONT_INFOEX));
    info.cbSize = sizeof(CONSOLE_FONT_INFOEX);              // prevents err=87 below
    if (GetCurrentConsoleFontEx(StdOut, FALSE, &info))
    {
        info.FontFamily   = FF_DONTCARE;
        info.dwFontSize.X = 0;  // leave X as zero
        info.dwFontSize.Y = 14;
        info.FontWeight   = 400;
        wcscpy(info.FaceName, font);
        if (SetCurrentConsoleFontEx(StdOut, FALSE, &info))
        {
        }
    }
}

int main()
{
  //setlocale(LC_ALL, "ru_RU.utf8");
  setlocale(LC_ALL, "");
  //SetConsoleCP(65001);
  set_console_font(L"Courier New");

  HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
  HANDLE stdin_handle = GetStdHandle(STD_INPUT_HANDLE);

  if(stdin_handle == INVALID_HANDLE_VALUE ||
     stdout_handle == INVALID_HANDLE_VALUE){
    cout << "Error getting console handles.\n";
    return 0;
  }

  SetConsoleTextAttribute(stdout_handle,
                          FOREGROUND_GREEN |
                          FOREGROUND_INTENSITY);
  SetConsoleTitleW(L"CardBluff");

  WINDOW* input_win;
  WINDOW* output_win;

  initscr();

  cbreak();             // Immediate key input
  nonl();               // Get return key
  timeout(0);           // Non-blocking input
  keypad(stdscr, 1);    // Fix keypad
  noecho();             // No automatic printing
  curs_set(0);          // Hide real cursor
  intrflush(stdscr, 0); // Avoid potential graphical issues
  leaveok(stdscr, 1);   // Don't care where cursor is left

  int rows, cols;
  getmaxyx(stdscr, rows, cols);

  refresh();

  output_win = newwin(rows - INPUT_WINDOW_ROWS, cols, 0, 0);
  refresh();
  scrollok(output_win, true);

  input_win = newwin(INPUT_WINDOW_ROWS, cols, rows - INPUT_WINDOW_ROWS, 0);
  refresh();
  use_win(input_win, [&](WINDOW* w){
            if(wclear(w) == ERR) return ERR;
            if(box(w, 0, 0) == ERR) return ERR;
            return wrefresh(w);
          });

  refresh();

  stdin_mutex = CreateMutex(NULL, FALSE, NULL);
  stdout_mutex = CreateMutex(NULL, FALSE, NULL);
  socket_mutex = CreateMutex(NULL, FALSE, NULL);

  curses_mutex = CreateMutex(NULL, FALSE, NULL);

  DWORD poll;
  int res, i = 1, port = 999;
  wchar_t buf[256];
  char msg[256] = "";
  char ip[15];
  WSADATA data;

  signal(SIGINT, s_handle);
  signal(SIGKILL, s_handle);
  signal(SIGQUIT, s_handle);

  //cout << "Enter IP to connect to: ";
  //gets(ip);

  //if(strlen(ip) == 0)
    strcpy(ip, "127.0.0.1");

  sockaddr_in ser;
  sockaddr addr;

  ser.sin_family = AF_INET;
  ser.sin_port = htons(PORT);                    //Set the port
  ser.sin_addr.s_addr = inet_addr(ip);      //Set the address we want to connect to

  memcpy(&addr, &ser, sizeof(SOCKADDR_IN));

  win_addwstr(output_win, L"WSA Startup... ");
  res = WSAStartup(MAKEWORD(1, 1), &data);      //Start Winsock

//  cout << "\nWSAStartup"
//       << "\nVersion: " << data.wVersion
//       << "\nDescription: " << data.szDescription
//       << "\nStatus: " << data.szSystemStatus << endl;

  if(res != 0)
      s_cl("failed",WSAGetLastError());

  win_addwstr(output_win, L"OK\n");
  win_addwstr(output_win, L"Creating socket... ");
  sock = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);       //Create the socket
      if(sock == INVALID_SOCKET )
          s_cl("Invalid Socket", WSAGetLastError());
      else if(sock == SOCKET_ERROR)
          s_cl("Socket Error", WSAGetLastError());
      else
        win_addwstr(output_win, L"OK\n");

  win_addwstr(output_win, L"Connecting to server... ");
  res = connect(sock,&addr,sizeof(addr));               //Connect to the server
  if(res != 0 ){
    s_cl("SERVER UNAVAILABLE", res);
  }
  else{
    win_addwstr(output_win, L"OK\n");
  }

  HANDLE terminate_event = CreateEvent(NULL, TRUE, FALSE, NULL);
  unsigned long non_blocking = 1;
  if (ioctlsocket(sock, FIONBIO, &non_blocking) == SOCKET_ERROR) {
    win_addwstr(output_win, L"Failed to put socket into non-blocking mode\n");
    closesocket(sock);
    return 0;
  }

  win_addwstr(output_win, L"Enter nickname:\n");

  state = ENTER_NICKNAME;

  HANDLE send_thread, receive_thread;
  DWORD send_th, receive_th;

  HANDLE thread_handles_ready_event = CreateEvent(NULL, TRUE, FALSE, NULL);
  //
  HANDLE nickname_request_event = CreateEvent(NULL, TRUE, TRUE, NULL);
  HANDLE pass_reg_first_request_event = CreateEvent(NULL, TRUE, FALSE, NULL);
  HANDLE pass_reg_second_request_event = CreateEvent(NULL, TRUE, FALSE, NULL);
  HANDLE password_request_event = CreateEvent(NULL, TRUE, FALSE, NULL);
  HANDLE authorized_event = CreateEvent(NULL, TRUE, FALSE, NULL);
  HANDLE auth_fail_event = CreateEvent(NULL, TRUE, FALSE, NULL);

  thread_params_t thread_params = {
    NULL,
    NULL,
    stdout_handle,
    stdin_handle,
    sock,
    thread_handles_ready_event,
    terminate_event,
    nickname_request_event,
    pass_reg_first_request_event,
    pass_reg_second_request_event,
    password_request_event,
    authorized_event,
    auth_fail_event,
    input_win,
    output_win
  };

  send_thread = CreateThread(NULL, 0, client_to_server, (LPVOID) (&thread_params), 0, &send_th);
  receive_thread = CreateThread(NULL, 0, server_to_client, (LPVOID) (&thread_params), 0, &receive_th);

  thread_params.send_thread = send_thread;
  thread_params.receive_thread = receive_thread;
  SetEvent(thread_handles_ready_event);

  WaitForSingleObject(send_thread, INFINITE);
  WaitForSingleObject(receive_thread, INFINITE);

  CloseHandle(thread_handles_ready_event);
  CloseHandle(terminate_event);
  CloseHandle(nickname_request_event);
  CloseHandle(pass_reg_first_request_event);
  CloseHandle(pass_reg_second_request_event);
  CloseHandle(password_request_event);
  CloseHandle(authorized_event);
  CloseHandle(auth_fail_event);

  CloseHandle(stdout_handle);
  CloseHandle(stdin_handle);

  CloseHandle(send_thread);
  CloseHandle(receive_thread);

  closesocket(client);
  WSACleanup();
}
