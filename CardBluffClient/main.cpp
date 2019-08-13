#include <windows.h>
#include <winsock.h>
#include <iostream>
#include <conio.h>
#include <signal.h>
#include <cstdio>
#include "sha256/sha256.h"
#include "util/strlcpy.h"
#include "util/unicode.h"

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
      getline(wcin, buf);
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
};

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

  HANDLE events[] = {
    terminate_event,
    nickname_request_event,
    pass_reg_first_request_event,
    pass_reg_second_request_event,
    password_request_event,
    authorized_event,
    auth_fail_event
  };

  wchar_t input_buffer[256];
  wchar_t send_buffer[SEND_BUFFER_SIZE];
  int res;
  bool should_send;
  wchar_t salt_str[17];

  bool _terminate = false;
  wstring pass;

  wprintf_mt(L"Send thread started.\n");

  while(true){
    should_send = true;
    switch(WaitForMultipleObjects(sizeof(events) / sizeof(HANDLE),
                                  events, FALSE, INFINITE)){
    case WAIT_OBJECT_0 + 0: //terminate
      wprintf_mt(L"Send thread terminated.\n");
      return 0;
      break;

    case WAIT_OBJECT_0 + 1:
      if(!cin_getline_mt(input_buffer, 256, stdin_handle, stdout_handle,
                         terminate_event, &_terminate) || _terminate){
        if(!_terminate)
          wprintf_mt(L"Error reading nickname from console.\n");
        if(WaitForSingleObject(terminate_event, 0) != WAIT_OBJECT_0){
          SetEvent(terminate_event);
          WaitForSingleObject(thread_handles_ready_event, INFINITE);
          WaitForSingleObject(params->receive_thread, INFINITE);
        }
        wprintf_mt(L"Send thread terminated.\n");
        return 0;
      }
      wcscpy(send_buffer, L"nickname:");
      wcscat(send_buffer, input_buffer);
      ResetEvent(nickname_request_event);
      break;

    case WAIT_OBJECT_0 + 2:
      if(!get_password(stdin_handle, stdout_handle, &pass,
                       terminate_event, &_terminate) || _terminate){
        if(!_terminate)
          wprintf_mt(L"Error reading password from console.\n");
        if(WaitForSingleObject(terminate_event, 0) != WAIT_OBJECT_0){
          SetEvent(terminate_event);
          WaitForSingleObject(thread_handles_ready_event, INFINITE);
          WaitForSingleObject(params->receive_thread, INFINITE);
        }
        wprintf_mt(L"Send thread terminated.\n");
        return 0;
      }
      wcscpy(input_buffer, pass.c_str());
      wcscpy(send_buffer, L"password_first:");
      wcscat(send_buffer, converter.from_bytes(sha256(converter.to_bytes(input_buffer))).c_str());
      ResetEvent(pass_reg_first_request_event);
      break;

    case WAIT_OBJECT_0 + 3:
      if(!get_password(stdin_handle, stdout_handle, &pass,
                       terminate_event, &_terminate) || _terminate){
        if(!_terminate)
          wprintf_mt(L"Error reading password from console.\n");
        if(WaitForSingleObject(terminate_event, 0) != WAIT_OBJECT_0){
          SetEvent(terminate_event);
          WaitForSingleObject(thread_handles_ready_event, INFINITE);
          WaitForSingleObject(params->receive_thread, INFINITE);
        }
        wprintf_mt(L"Send thread terminated.\n");
        return 0;
      }
      wcscpy(input_buffer, pass.c_str());
      wcscpy(send_buffer, L"password_second:");
      swprintf(salt_str, L"%08x%08x",
              (unsigned int) (salt >> 32),
              (unsigned int) (salt & 0xFFFFFFFF));
      wcscpy(input_buffer, converter.from_bytes(sha256(converter.to_bytes(input_buffer))).c_str());
      wcscat(input_buffer, salt_str);
      wcscat(send_buffer, converter.from_bytes(sha256(converter.to_bytes(input_buffer))).c_str());
      ResetEvent(pass_reg_second_request_event);
      break;

    case WAIT_OBJECT_0 + 4:
      if(!get_password(stdin_handle, stdout_handle, &pass,
                       terminate_event, &_terminate) || _terminate){
        if(!_terminate)
          wprintf_mt(L"Error reading password from console.\n");
        if(WaitForSingleObject(terminate_event, 0) != WAIT_OBJECT_0){
          SetEvent(terminate_event);
          WaitForSingleObject(thread_handles_ready_event, INFINITE);
          WaitForSingleObject(params->receive_thread, INFINITE);
        }
        wprintf_mt(L"Send thread terminated.\n");
        return 0;
      }
      wcscpy(input_buffer, pass.c_str());
      wcscpy(send_buffer, L"password:");
      swprintf(salt_str, L"%08x%08x",
              (unsigned int) (salt >> 32),
              (unsigned int) (salt & 0xFFFFFFFF));
      wcscpy(input_buffer, converter.from_bytes(sha256(converter.to_bytes(input_buffer))).c_str());
      wcscat(input_buffer, salt_str);
      wcscat(send_buffer, converter.from_bytes(sha256(converter.to_bytes(input_buffer))).c_str());
      ResetEvent(password_request_event);
      break;

    case WAIT_OBJECT_0 + 5: //authorized
      if(!cin_getline_mt(input_buffer, 256, stdin_handle, stdout_handle,
                         terminate_event, &_terminate) || _terminate){
        if(!_terminate)
          wprintf_mt(L"Error reading from console.");
        if(WaitForSingleObject(terminate_event, 0) != WAIT_OBJECT_0){
          SetEvent(terminate_event);
          WaitForSingleObject(thread_handles_ready_event, INFINITE);
          WaitForSingleObject(params->receive_thread, INFINITE);
        }
        wprintf_mt(L"Send thread terminated.\n");
        return 0;
      }
      wcscpy(send_buffer, input_buffer);
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
          wprintf_mt(L"Connection error. Finishing all threads...\n");
          if(WaitForSingleObject(terminate_event, 0) != WAIT_OBJECT_0){
            SetEvent(terminate_event);
            WaitForSingleObject(thread_handles_ready_event, INFINITE);
            WaitForSingleObject(params->receive_thread, INFINITE);
          }
          wprintf_mt(L"Send thread terminated.\n");
          return 0;
        }
        if(WaitForSingleObject(terminate_event, 0) == WAIT_OBJECT_0){
          wprintf_mt(L"Send thread terminated.\n");
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

  char receive_buffer_raw[RECEIVE_BUFFER_SIZE];
  wstring receive_buffer;
  int res;
  wprintf_mt(L"Receive thread started.\n");
  while(true){
    while(recv(socket, receive_buffer_raw, RECEIVE_BUFFER_SIZE, 0) == SOCKET_ERROR){
      if(WSAGetLastError() != WSAEWOULDBLOCK){
        wprintf_mt(L"Connection error. Finishing all threads...\n");
        if(WaitForSingleObject(terminate_event, 0) != WAIT_OBJECT_0){
          SetEvent(terminate_event);
          WaitForSingleObject(thread_handles_ready_event, INFINITE);
          WaitForSingleObject(params->send_thread, INFINITE);
        }
        wprintf_mt(L"Receive thread terminated.\n");
        return 0;
      }
      if(WaitForSingleObject(terminate_event, 0) == WAIT_OBJECT_0){
        wprintf_mt(L"Receive thread terminated.\n");
        return 0;
      }
      Sleep(10);
    }

    receive_buffer = converter.from_bytes(receive_buffer_raw);

    if(wcscmp(receive_buffer.c_str(), L"") != 0){
      if(WaitForSingleObject(authorized_event, 0) == WAIT_OBJECT_0){
        wprintf_mt(L"> %ls\n", receive_buffer.c_str());
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
          wprintf_mt(L"> %ls\n", receive_buffer.c_str());
      }
    }
  }
}

int main()
{

  setlocale(LC_ALL, "ru_RU.utf8");
  SetConsoleCP(65001);

  HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
  HANDLE stdin_handle = GetStdHandle(STD_INPUT_HANDLE);

  if(stdin_handle == INVALID_HANDLE_VALUE ||
     stdout_handle == INVALID_HANDLE_VALUE){
    wprintf_mt(L"Error getting console handles.\n");
    return 0;
  }

  SetConsoleTextAttribute(stdout_handle,
                          FOREGROUND_GREEN |
                          FOREGROUND_INTENSITY);
  SetConsoleTitleW(L"CardBluff");

  stdin_mutex = CreateMutex(NULL, FALSE, NULL);
  stdout_mutex = CreateMutex(NULL, FALSE, NULL);
  socket_mutex = CreateMutex(NULL, FALSE, NULL);

  DWORD poll;
  int res, i = 1, port = 999;
  wchar_t buf[256];
  char msg[256] = "";
  char ip[15];
  WSADATA data;

  signal(SIGINT, s_handle);
  signal(SIGKILL, s_handle);
  signal(SIGQUIT, s_handle);

  cout << "Enter IP to connect to: ";
  gets(ip);

  if(strlen(ip) == 0)
    strcpy(ip, "127.0.0.1");

  sockaddr_in ser;
  sockaddr addr;

  ser.sin_family = AF_INET;
  ser.sin_port = htons(PORT);                    //Set the port
  ser.sin_addr.s_addr = inet_addr(ip);      //Set the address we want to connect to

  memcpy(&addr, &ser, sizeof(SOCKADDR_IN));

  res = WSAStartup(MAKEWORD(1, 1), &data);      //Start Winsock
  cout << "\nWSAStartup"
       << "\nVersion: " << data.wVersion
       << "\nDescription: " << data.szDescription
       << "\nStatus: " << data.szSystemStatus << endl;

  if(res != 0)
      s_cl("WSAStarup failed",WSAGetLastError());

  sock = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);       //Create the socket
      if(sock == INVALID_SOCKET )
          s_cl("Invalid Socket", WSAGetLastError());
      else if(sock == SOCKET_ERROR)
          s_cl("Socket Error", WSAGetLastError());
      else
          cout << "Socket Established" << endl;



  res = connect(sock,&addr,sizeof(addr));               //Connect to the server
  if(res != 0 ){
    s_cl("SERVER UNAVAILABLE", res);
  }
  else{
    cout<<"\nConnected to Server: \n";
    memcpy(&ser, &addr, sizeof(SOCKADDR));
  }



  cout << "Enter nickname: ";

  state = ENTER_NICKNAME;

  wstring nickname;
  getline(wcin, nickname);

  nickname = L"nickname:" + nickname;

  char buf_raw[SEND_BUFFER_SIZE];
  strlcpy(buf_raw, converter.to_bytes(nickname).c_str(), SEND_BUFFER_SIZE);

  printf_mt("nick: %ls\n", nickname.c_str());
  printf_mt("raw: %s\n", buf_raw);

  send(sock, buf_raw, sizeof(buf_raw), 0);

  unsigned long non_blocking = 1;
  if (ioctlsocket(sock, FIONBIO, &non_blocking) == SOCKET_ERROR) {
    wprintf_mt(L"Failed to put socket into non-blocking mode\n");
    closesocket(sock);
    return 0;
  }

  HANDLE send_thread, receive_thread;
  DWORD send_th, receive_th;

  HANDLE thread_handles_ready_event = CreateEvent(NULL, TRUE, FALSE, NULL);
  HANDLE terminate_event = CreateEvent(NULL, TRUE, FALSE, NULL);
  HANDLE nickname_request_event = CreateEvent(NULL, TRUE, FALSE, NULL);
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
    auth_fail_event
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
