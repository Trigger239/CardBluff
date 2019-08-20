#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <winsock.h>
#include <iostream>
//#include <conio.h>
#include <signal.h>
#include <cstdio>
//#include <functional>
#include <cctype>
#include <chrono>

#include "sha256/sha256.h"
#include "util/strlcpy.h"
#include "util/unicode.h"
//#include "util/lambda_to_func.h"
#include "text_io.h"

using namespace std;

//DECLARATIONS
//error trapping signals
#define SIGINT 2
#define SIGKILL 9
#define SIGQUIT 3

#define RECEIVE_BUFFER_SIZE 256
#define SEND_BUFFER_SIZE 256

#define PORT 2390

#define NICKNAME_SIZE_MIN 3
#define NICKNAME_CHARS_NOT_ALLOWED L":"
#define PASSWORD_SIZE_MIN 4

typedef enum{
  NOT_CONNECTED,
  CONNECTION_LOST,
  CONNECT,

  WAIT_RECONNECT_OR_QUIT,

  NICKNAME_ENTER,
  NICKNAME_SENT,

  PASSWORD_REGISTER_FIRST_ENTER,
  PASSWORD_REGISTER_FIRST_SENT,

  PASSWORD_REGISTER_SECOND_ENTER,
  PASSWORD_REGISTER_SECOND_SENT,

  PASSWORD_ENTER,
  PASSWORD_SENT,

  AUTHORIZED,
  AUTHORIZATION_FAILED
} state_t;

SOCKET _socket;

void s_handle(int s)
{
  if(_socket)
    closesocket(_socket);
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

SOCKET create_socket(){
  win_addwstr(output_win, L"Creating socket... ");
  SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);       //Create the socket
      if(sock == INVALID_SOCKET )
          s_cl("Invalid Socket", WSAGetLastError());
      else if(sock == SOCKET_ERROR)
          s_cl("Socket Error", WSAGetLastError());
      else
        win_addwstr(output_win, L"OK\n");
  return sock;
}

int main(void){
  WSADATA data;
  int res;
  char ip[17];

  console_init();

  win_addwstr(output_win, L"WSA Startup... ");
  res = WSAStartup(MAKEWORD(1, 1), &data);      //Start Winsock

  if(res != 0)
      s_cl("failed", WSAGetLastError());

  win_addwstr(output_win, L"OK\n");

  strcpy(ip, "127.0.0.1");

  sockaddr_in ser;
  sockaddr addr;

  ser.sin_family = AF_INET;
  ser.sin_port = htons(PORT);                    //Set the port
  ser.sin_addr.s_addr = inet_addr(ip);      //Set the address we want to connect to

  memcpy(&addr, &ser, sizeof(SOCKADDR_IN));

  signal(SIGINT, s_handle);
  signal(SIGKILL, s_handle);
  signal(SIGQUIT, s_handle);

  SOCKET socket;

  bool quit = false;
  bool connected = false;
  state_t state = CONNECT;

  wchar_t send_buffer[SEND_BUFFER_SIZE];
  char receive_buffer_raw[RECEIVE_BUFFER_SIZE];

  wchar_t salt_str[17];
  unsigned long long salt;

  wstring input_buffer;
  wstring receive_buffer;
  wstring pass;


  while(!quit){
    if(connected){
      if(recv(socket, receive_buffer_raw, RECEIVE_BUFFER_SIZE, 0) == SOCKET_ERROR){
        if(WSAGetLastError() != WSAEWOULDBLOCK){
          state = CONNECTION_LOST;
          connected = false;
        }
      }
      else{
        receive_buffer = converter.from_bytes(receive_buffer_raw);

        if(wcscmp(receive_buffer.c_str(), L"") != 0){
          if(state == AUTHORIZED){
            wchar_t* buf = new wchar_t[receive_buffer.size() + 1];
            wcscpy(buf, receive_buffer.c_str());
            win_addwstr_colored(output_win, buf);
            delete[] buf;
          }
          else{
            if(wcscmp(receive_buffer.c_str(), L"password_first?") == 0){
              state = PASSWORD_REGISTER_FIRST_ENTER;
            }
            else if(wcsncmp(receive_buffer.c_str(), L"password_second?", 16) == 0){
              unsigned int salt_h, salt_l;
              swscanf(receive_buffer.c_str() + 16, L"%8x%8x", &salt_h, &salt_l);
              salt = (((unsigned long long) salt_h) << 32) | salt_l;
              state = PASSWORD_REGISTER_SECOND_ENTER;
            }
            else if(wcsncmp(receive_buffer.c_str(), L"password?", 9) == 0){
              unsigned int salt_h, salt_l;
              swscanf(receive_buffer.c_str() + 9, L"%8x%8x", &salt_h, &salt_l);
              salt = (((unsigned long long) salt_h) << 32) | salt_l;
              state = PASSWORD_ENTER;
            }
            else if(wcscmp(receive_buffer.c_str(), L"auth_ok!") == 0){
              state = AUTHORIZED;
            }
            else if(wcscmp(receive_buffer.c_str(), L"auth_fail!") == 0){
              state = AUTHORIZATION_FAILED;
            }
            else{
              wchar_t* buf = new wchar_t[receive_buffer.size() + 1];
              wcscpy(buf, receive_buffer.c_str());
              win_addwstr_colored(output_win, buf);
              delete[] buf;
            }
              //win_addwstr(output_win, (L"> " + receive_buffer + L"\n").c_str());
              //win_wprintw(output_win, "> %s\n", receive_buffer.c_str());
          }
        }
      }
    }

    bool input_ready = false;
    bool should_send = false;
    wchar_t c;

    switch(state){
    case CONNECTION_LOST:
      closesocket(socket);
      win_addwstr_colored(output_win, ERROR_PREFIX L" Connection is lost!");
      win_addwstr(output_win, L"Press Enter to try to connect again,");
      win_addwstr(output_win, L"or press 'Q' to quit.\n");
      state = WAIT_RECONNECT_OR_QUIT;
      break;

    case WAIT_RECONNECT_OR_QUIT:
      c = getch();
      if(c == (wchar_t) ERR){
        Sleep(10);
        break;
      }

      if(c & KEY_CODE_YES){
        if(c == KEY_ENTER || c == PADENTER){
          state = CONNECT;
        }
        break;
      }

      if(c == L'\n' || c == L'\r'){
        state = CONNECT;
        break;
      }

      if(c == L'Q' || c == L'q')
        quit = true;

      break;

    case CONNECT:
      _socket = create_socket();
      socket = _socket;

      win_addwstr(output_win, L"Connecting to server...\n");
      res = connect(socket, &addr, sizeof(addr)); //Connect to the server
      if(res == 0){
        win_addwstr(output_win, L"Connection established!\n");

        unsigned long non_blocking = 1;
        if (ioctlsocket(socket, FIONBIO, &non_blocking) != SOCKET_ERROR){
          win_addwstr(output_win, L"Connection parameters were set successfully.\n");

          win_addwstr(output_win, L"Please enter nickname.\n");

          connected = true;
          state = NICKNAME_ENTER;
        }
        else{
          win_addwstr_colored(output_win, ERROR_PREFIX L" Failed to put socket into non-blocking mode!");
          closesocket(socket);

          win_addwstr(output_win, L"Press Enter to try to connect again,");
          win_addwstr(output_win, L"or press 'Q' to quit.\n");

          state = WAIT_RECONNECT_OR_QUIT;
        }
      }
      else{
        win_addwstr_colored(output_win, ERROR_PREFIX L" Server unavailable!");
        closesocket(socket);

        win_addwstr(output_win, L"Press Enter to try to connect again,");
        win_addwstr(output_win, L"or press 'Q' to quit.\n");

        state = WAIT_RECONNECT_OR_QUIT;
      }
      break;

    case NICKNAME_ENTER:
      if(!win_get_wstr(input_win, output_win, input_buffer, false, &input_ready)){
        win_addwstr_colored(output_win, ERROR_PREFIX L" Can't read nickname from console.\n");
      }
      if(!input_ready)
        break;

      if(input_buffer.size() < NICKNAME_SIZE_MIN){
        wchar_t buf[100];
        swprintf(buf, ERROR_PREFIX L" Nickname should contain at least %u characters!", (unsigned int) NICKNAME_SIZE_MIN);
        win_addwstr_colored(output_win, buf);
        win_addwstr(output_win, L"Please try again.\n");
        break;
      }
      if(wcscspn(input_buffer.c_str(), NICKNAME_CHARS_NOT_ALLOWED) < input_buffer.size()){
        wchar_t buf[100];
        swprintf(buf, ERROR_PREFIX L" Nickname shouldn't contain these characters: %ls", (unsigned int) NICKNAME_CHARS_NOT_ALLOWED);
        win_addwstr_colored(output_win, buf);
        win_addwstr(output_win, L"Please try again.\n");
        break;
      }
      input_buffer = L"nickname:" + input_buffer;
      wcslcpy(send_buffer, input_buffer.c_str(), SEND_BUFFER_SIZE);
      should_send = true;
      state = NICKNAME_SENT;
      break;

    case PASSWORD_REGISTER_FIRST_ENTER:
      if(!win_get_wstr(input_win, output_win, pass, true, &input_ready)){
        win_addwstr_colored(output_win, ERROR_PREFIX L" Can't read password from console.\n");
      }
      if(!input_ready)
        break;

      if(pass.size() < PASSWORD_SIZE_MIN){
        wchar_t buf[100];
        swprintf(buf, ERROR_PREFIX L" Password should contain at least %u characters!", (unsigned int) PASSWORD_SIZE_MIN);
        win_addwstr_colored(output_win, buf);
        win_addwstr(output_win, L"Please try again.\n");
        break;
      }
      wcscpy(send_buffer, L"password_first:");
      wcscat(send_buffer, converter.from_bytes(sha256(converter.to_bytes(pass))).c_str());
      should_send = true;
      state = PASSWORD_REGISTER_FIRST_SENT;
      break;

    case PASSWORD_REGISTER_SECOND_ENTER:
      if(!win_get_wstr(input_win, output_win, pass, true, &input_ready)){
        win_addwstr_colored(output_win, ERROR_PREFIX L" Can't read password from console.\n");
      }
      if(!input_ready)
        break;

      if(pass.size() < PASSWORD_SIZE_MIN){
        wchar_t buf[100];
        swprintf(buf, ERROR_PREFIX L" Password should contain at least %u characters!", (unsigned int) PASSWORD_SIZE_MIN);
        win_addwstr_colored(output_win, buf);
        win_addwstr(output_win, L"Please try again.\n");
        break;
      }
      swprintf(salt_str, L"%08x%08x",
              (unsigned int) (salt >> 32),
              (unsigned int) (salt & 0xFFFFFFFF));
      pass = converter.from_bytes(sha256(converter.to_bytes(pass)));
      pass += wstring(salt_str);
      pass = converter.from_bytes(sha256(converter.to_bytes(pass)));

      pass = wstring(L"password_second:") + pass;

      wcscpy(send_buffer, pass.c_str());

      should_send = true;
      state = PASSWORD_REGISTER_SECOND_SENT;
      break;

    case PASSWORD_ENTER:
      if(!win_get_wstr(input_win, output_win, pass, true, &input_ready)){
        win_addwstr_colored(output_win, ERROR_PREFIX L" Can't read password from console.\n");
      }
      if(!input_ready)
        break;

      if(pass.size() < PASSWORD_SIZE_MIN){
        wchar_t buf[100];
        swprintf(buf, ERROR_PREFIX L" Password should contain at least %u characters!", (unsigned int) PASSWORD_SIZE_MIN);
        win_addwstr_colored(output_win, buf);
        win_addwstr(output_win, L"Please try again.\n");
        break;
      }
      swprintf(salt_str, L"%08x%08x",
              (unsigned int) (salt >> 32),
              (unsigned int) (salt & 0xFFFFFFFF));
      pass = converter.from_bytes(sha256(converter.to_bytes(pass)));
      pass += wstring(salt_str);
      pass = converter.from_bytes(sha256(converter.to_bytes(pass)));

      pass = wstring(L"password:") + pass;

      wcscpy(send_buffer, pass.c_str());

      should_send = true;
      state = PASSWORD_SENT;
      break;

    case AUTHORIZED:
      if(!win_get_wstr(input_win, output_win, input_buffer, false, &input_ready)){
        win_addwstr_colored(output_win, ERROR_PREFIX L" Can't read from console.\n");
      }
      if(!input_ready)
        break;
      wcslcpy(send_buffer, input_buffer.c_str(), SEND_BUFFER_SIZE);
      should_send = true;
      break;

    case AUTHORIZATION_FAILED:
      connected = false;
      state = CONNECTION_LOST;
      break;
    }

    if(!quit && connected){
      if(should_send){
        char send_buffer_raw[SEND_BUFFER_SIZE];
        strlcpy(send_buffer_raw, converter.to_bytes(send_buffer).c_str(), SEND_BUFFER_SIZE);

        if(send(socket, send_buffer_raw, SEND_BUFFER_SIZE, 0) == SOCKET_ERROR){
          if(WSAGetLastError() != WSAEWOULDBLOCK){
            state = CONNECTION_LOST;
            connected = false;
          }
        }
        else{
          should_send = false;
        }
      }
    }

  }
  s_cl("", 0);

}















//HANDLE stdin_mutex;
//HANDLE stdout_mutex;
//HANDLE socket_mutex;
//
//
//
//struct thread_params_t{
//  HANDLE send_thread;
//  HANDLE receive_thread;
//  SOCKET socket;
//  HANDLE thread_handles_ready_event;
//  HANDLE terminate_event;
//  HANDLE connected_event;
//  HANDLE nickname_request_event;
//  HANDLE pass_reg_first_request_event;
//  HANDLE pass_reg_second_request_event;
//  HANDLE password_request_event;
//  HANDLE authorized_event;
//  HANDLE auth_fail_event;
//  HANDLE recv_error_event;
//  WINDOW* input_win;
//  WINDOW* output_win;
//};
//
//
//DWORD WINAPI client_to_server(LPVOID lpParam){
//  thread_params_t* params = (thread_params_t*) lpParam;
//  SOCKET socket = params->socket;
//  HANDLE thread_handles_ready_event = params->terminate_event;
//  HANDLE terminate_event = params->terminate_event;
//  HANDLE connected_event = params->connected_event;
//  HANDLE nickname_request_event = params->nickname_request_event;
//  HANDLE pass_reg_first_request_event = params->pass_reg_first_request_event;
//  HANDLE pass_reg_second_request_event = params->pass_reg_second_request_event;
//  HANDLE password_request_event = params->password_request_event;
//  HANDLE authorized_event = params->authorized_event;
//  HANDLE auth_fail_event = params->auth_fail_event;
//  HANDLE recv_error_event = params->recv_error_event;
//  WINDOW* input_win = params->input_win;
//  WINDOW* output_win = params->output_win;
//
//  HANDLE events[] = {
//    terminate_event,
//    nickname_request_event,
//    pass_reg_first_request_event,
//    pass_reg_second_request_event,
//    password_request_event,
//    authorized_event,
//    auth_fail_event
//  };
//
//  wstring input_buffer;
//
//  wchar_t send_buffer[SEND_BUFFER_SIZE];
//  int res;
//  bool should_send;
//  wchar_t salt_str[17];
//
//  bool _terminate = false;
//  bool need_to_reconnect = false;
//  wstring pass;
//
//  win_addwstr(output_win, L"Send thread started.\n");
//
//  while(true){
//    if(WaitForSingleObject(connected_event, 0) == WAIT_OBJECT_0){ //connected
//
//      should_send = true;
//      EnterCriticalSection(socket_error_cs);
//      DWORD ev = WaitForMultipleObjects(sizeof(events) / sizeof(HANDLE),
//                                    events, FALSE, 0);
//      if(ev == WAIT_OBJECT_0){ //terminate
//        if(WaitForSingleObject(socket_error_event, 0) != WAIT_OBJECT_0){
//          win_addwstr(output_win, L"Send thread terminated.\n");
//          LeaveCriticalSection(socket_error_cs);
//          return 0;
//        }
//      }
//      LeaveCriticalSection(socket_error_cs);
//
//      switch(ev){
//      case WAIT_TIMEOUT:
//        Sleep(10);
//        break;
//
//      case WAIT_OBJECT_0 + 1: //Enter nickname
//        if(!win_get_wstr(input_win, output_win, input_buffer, false,
//                         terminate_event, &_terminate) || _terminate){
//          if(!_terminate)
//            win_addwstr(output_win, ERROR_PREFIX L"Can't read nickname from console.\n");
//          else{
//            EnterCriticalSection(socket_error_cs);
//            if(WaitForSingleObject(terminate_event, 0) == WAIT_OBJECT_0){
//              if(WaitForSingleObject(socket_error_event, 0) != WAIT_OBJECT_0){
//                win_addwstr(output_win, L"Send thread terminated.\n");
//                LeaveCriticalSection(socket_error_cs);
//                return 0;
//              }
//            }
//            LeaveCriticalSection(socket_error_cs);
//          }
//        }
//        if(input_buffer.size() < NICKNAME_SIZE_MIN){
//          wchar_t buf[100];
//          swprint(buf, EERROR_PREFIX L"Nickname should contain at least %u characters!", (unsigned int) NICKNAME_SIZE_MIN);
//          win_addwstr(output_win, buf);
//          win_addwstr(output_win, L"Please try again.");
//          break;
//        }
//        if(wcscspn(input_buffer.c_str(), NICKNAME_CHARS_NOT_ALLOWED) < input_buffer.size()){
//          wchar_t buf[100];
//          swprint(buf, EERROR_PREFIX L"Nickname shouldn't contain these characters: %ls", (unsigned int) NICKNAME_CHARS_NOT_ALLOWED);
//          win_addwstr(output_win, buf);
//          win_addwstr(output_win, L"Please try again.");
//          break;
//        }
//        input_buffer = L"nickname:" + input_buffer;
//        wcslcpy(send_buffer, input_buffer.c_str(), SEND_BUFFER_SIZE);
//        //ResetEvent(nickname_request_event);
//        break;
//
//      case WAIT_OBJECT_0 + 2: //password_first
//        if(!win_get_wstr(input_win, output_win, pass, true,
//                         terminate_event, &_terminate) || _terminate){
//          if(!_terminate)
//            win_addwstr(output_win, ERROR_PREFIX L"Can't read password from console.\n");
//          else if(WaitForSingleObject(terminate_event, 0) != WAIT_OBJECT_0){
//            SetEvent(terminate_event);
//            win_addwstr(output_win, L"Send thread terminated.\n");
//            return 0;
//          }
//        }
//        if(pass.size() < PASSWORD_SIZE_MIN){
//          wchar_t buf[100];
//          swprint(buf, EERROR_PREFIX L"Password should contain at least %u characters!", (unsigned int) PASSWORD_SIZE_MIN);
//          win_addwstr(output_win, buf);
//          win_addwstr(output_win, L"Please try again.");
//          break;
//        }
//        wcscpy(send_buffer, L"password_first:");
//        wcscat(send_buffer, converter.from_bytes(sha256(converter.to_bytes(pass))).c_str());
//        //ResetEvent(pass_reg_first_request_event);
//        break;
//
//      case WAIT_OBJECT_0 + 3: //password_second
//        if(!win_get_wstr(input_win, output_win, pass, true,
//                         terminate_event, &_terminate) || _terminate){
//          if(!_terminate)
//            win_addwstr(output_win, ERROR_PREFIX L"Can't read password from console.\n");
//          else if(WaitForSingleObject(terminate_event, 0) != WAIT_OBJECT_0){
//            SetEvent(terminate_event);
//            win_addwstr(output_win, L"Send thread terminated.\n");
//            return 0;
//          }
//        }
//        if(pass.size() < PASSWORD_SIZE_MIN){
//          wchar_t buf[100];
//          swprint(buf, EERROR_PREFIX L"Password should contain at least %u characters!", (unsigned int) PASSWORD_SIZE_MIN);
//          win_addwstr(output_win, buf);
//          win_addwstr(output_win, L"Please try again.");
//          break;
//        }
//        swprintf(salt_str, L"%08x%08x",
//                (unsigned int) (salt >> 32),
//                (unsigned int) (salt & 0xFFFFFFFF));
//        pass = converter.from_bytes(sha256(converter.to_bytes(pass)));
//        pass += wstring(salt_str);
//        pass = converter.from_bytes(sha256(converter.to_bytes(pass)));
//
//        pass = wstring(L"password_second:") + pass;
//
//        wcscpy(send_buffer, pass.c_str());
//        //ResetEvent(pass_reg_second_request_event);
//        break;
//
//      case WAIT_OBJECT_0 + 4: //password
//        if(!win_get_wstr(input_win, output_win, pass, true,
//                         terminate_event, &_terminate) || _terminate){
//          if(!_terminate)
//            win_addwstr(output_win, ERROR_PREFIX L"Can't read password from console.\n");
//          else if(WaitForSingleObject(terminate_event, 0) != WAIT_OBJECT_0){
//            SetEvent(terminate_event);
//            win_addwstr(output_win, L"Send thread terminated.\n");
//            return 0;
//          }
//        }
//        if(pass.size() < PASSWORD_SIZE_MIN){
//          wchar_t buf[100];
//          swprint(buf, EERROR_PREFIX L"Password should contain at least %u characters!", (unsigned int) PASSWORD_SIZE_MIN);
//          win_addwstr(output_win, buf);
//          win_addwstr(output_win, L"Please try again.");
//          break;
//        }
//        swprintf(salt_str, L"%08x%08x",
//                (unsigned int) (salt >> 32),
//                (unsigned int) (salt & 0xFFFFFFFF));
//        pass = converter.from_bytes(sha256(converter.to_bytes(pass)));
//        pass += wstring(salt_str);
//        pass = converter.from_bytes(sha256(converter.to_bytes(pass)));
//
//        pass = wstring(L"password:") + pass;
//
//        wcscpy(send_buffer, pass.c_str());
//        //ResetEvent(password_request_event);
//        break;
//
//      case WAIT_OBJECT_0 + 5: //authorized
//        if(!win_get_wstr(input_win, output_win, input_buffer, false,
//                          terminate_event, &_terminate) || _terminate){
//          if(!_terminate)
//            win_addwstr(output_win, ERROR_PREFIX L"Can't read from console.\n");
//          else if(WaitForSingleObject(terminate_event, 0) != WAIT_OBJECT_0){
//            SetEvent(terminate_event);
//            win_addwstr(output_win, L"Send thread terminated.\n");
//            return 0;
//          }
//        }
//        wcslcpy(send_buffer, input_buffer.c_str(), SEND_BUFFER_SIZE);
//        break;
//
//      default:
//        Sleep(10);
//        should_send = false;
//        break;
//      }
//
//      if(should_send){
//        char send_buffer_raw[SEND_BUFFER_SIZE];
//        strlcpy(send_buffer_raw, converter.to_bytes(send_buffer).c_str(), SEND_BUFFER_SIZE);
//
//        while(send(socket, send_buffer_raw, SEND_BUFFER_SIZE, 0) == SOCKET_ERROR){
//          if(WSAGetLastError() != WSAEWOULDBLOCK){
//            win_addwstr(output_win, ERROR_PREFIX L" Connection is lost!");
//            win_addwstr(output_win, L"Press Enter to try to connect again,");
//            win_addwstr(output_win, L"or press 'Q' to quit.");
//
//            ResetEvent(connected_event);
//
//            while(true){
//              wchar_t c = getch();
//              if(c == (wchar_t) ERR){
//                if(WaitForSingleObject(terminate_event, 0) == WAIT_OBJECT_0){
//                  win_addwstr(output_win, L"Send thread terminated.\n");
//                  return 0;
//                }
//                Sleep(10);
//                continue;
//              }
//
//              if(c & KEY_CODE_YES){
//                if(c == KEY_ENTER || c == PADENTER){
//                  break;
//                }
//                continue;
//              }
//
//              if(c == L'Q' || c == L'q'){
//                SetEvent(terminate_event);
//                win_addwstr(output_win, L"Send thread terminated.\n");
//                return 0;
//              }
//            }
//          }
//          if(WaitForSingleObject(terminate_event, 0) == WAIT_OBJECT_0){
//            win_addwstr(output_win, L"Send thread terminated.\n");
//            return 0;
//          }
//          Sleep(10);
//        }
//      }
//    }
//    else{ //Not connected
//      bool need_to_reconnect = false;
//
//      win_addwstr(output_win, L"Connecting to server...");
//      res = connect(sock,&addr,sizeof(addr)); //Connect to the server
//      if(res == 0){
//        win_addwstr(output_win, L"Connection established!");
//
//        unsigned long non_blocking = 1;
//        if (ioctlsocket(sock, FIONBIO, &non_blocking) != SOCKET_ERROR){
//          win_addwstr(output_win, L"Connection parameters were set successfully.");
//
//          win_addwstr(output_win, L"Please enter nickname...");
//
//          SetEvent(connected_event);
//        }
//        else{
//          win_addwstr(output_win, ERROR_PREFIX L"Failed to put socket into non-blocking mode!");
//          need_to_reconnect = true;
//        }
//      }
//      else{
//        win_addwstr(output_win, ERROR_PREFIX L"Server unavailable!");
//        need_to_reconnect = true;
//      }
//
//      if(need_to_reconnect){
//        win_addwstr(output_win, L"Press Enter to try to connect again,");
//        win_addwstr(output_win, L"or press 'Q' to quit.");
//        while(true){
//          wchar_t c = getch();
//          if(c == (wchar_t) ERR){
//            if(WaitForSingleObject(terminate_event, 0) == WAIT_OBJECT_0){
//              win_addwstr(output_win, L"Send thread terminated.\n");
//              return 0;
//            }
//            Sleep(10);
//            continue;
//          }
//
//          if(c & KEY_CODE_YES){
//            if(c == KEY_ENTER || c == PADENTER){
//              break;
//            }
//            continue;
//          }
//
//          if(c == L'Q' || c == L'q'){
//            SetEvent(terminate_event);
//            win_addwstr(output_win, L"Send thread terminated.\n");
//            return 0;
//          }
//        }
//      }
//    }
//  }
//}
//
//DWORD WINAPI server_to_client(LPVOID lpParam){
//  thread_params_t* params = (thread_params_t*) lpParam;
//  SOCKET socket = params->socket;
//  HANDLE thread_handles_ready_event = params->terminate_event;
//  HANDLE terminate_event = params->terminate_event;
//  HANDLE nickname_request_event = params->nickname_request_event;
//  HANDLE pass_reg_first_request_event = params->pass_reg_first_request_event;
//  HANDLE pass_reg_second_request_event = params->pass_reg_second_request_event;
//  HANDLE password_request_event = params->password_request_event;
//  HANDLE authorized_event = params->authorized_event;
//  HANDLE auth_fail_event = params->auth_fail_event;
//  HANDLE recv_error_event = params->recv_error_event;
//  HANDLE send_error_event = params->send_error_event;
//  WINDOW* input_win = params->input_win;
//  WINDOW* output_win = params->output_win;
//
//  char receive_buffer_raw[RECEIVE_BUFFER_SIZE];
//  wstring receive_buffer;
//  int res;
//  win_addwstr(output_win, L"Receive thread started.\n");
//  while(true){
//    if(WaitForSingleObject(connected_event, 0) == WAIT_OBJECT_0){  //connected
//      while(recv(socket, receive_buffer_raw, RECEIVE_BUFFER_SIZE, 0) == SOCKET_ERROR){
//        if(WSAGetLastError() != WSAEWOULDBLOCK){
//          EnterCriticalSection(socket_error_cs);
//          if(WaitForSingleObject(connected_event, 0) == WAIT_OBJECT_0){
//            ResetEvent(connected_event);
//            SetEvent(socket_error_event);
//            SetEvent(terminate_event);
//
//            win_addwstr(output_win, ERROR_PREFIX L" Connection is lost!");
//            win_addwstr(output_win, L"Press Enter to try to connect again,");
//            win_addwstr(output_win, L"or press 'Q' to quit.");
//          }
//          LeaveCriticalSection(socket_error_cs);
//        }
//
//        EnterCriticalSection(socket_error_cs);
//        if(WaitForSingleObject(terminate_event, 0) == WAIT_OBJECT_0){
//          if(WaitForSingleObject(socket_error_event, 0) != WAIT_OBJECT_0){
//            win_addwstr(output_win, L"Receive thread terminated.\n");
//            LeaveCriticalSection(socket_error_cs);
//            return 0;
//          }
//        }
//        LeaveCriticalSection(socket_error_cs);
//        Sleep(10);
//      }
//
//      receive_buffer = converter.from_bytes(receive_buffer_raw);
//
//      if(wcscmp(receive_buffer.c_str(), L"") != 0){
//        if(WaitForSingleObject(authorized_event, 0) == WAIT_OBJECT_0){
//          wchar_t* buf = new wchar_t[receive_buffer.size() + 1];
//          wcscpy(buf, receive_buffer.c_str());
//          win_addwstr_colored(output_win, buf);
//          delete[] buf;
//        }
//        else{
//          if(wcscmp(receive_buffer.c_str(), L"password_first?") == 0){
//            SetEvent(pass_reg_first_request_event);
//          }
//          else if(wcsncmp(receive_buffer.c_str(), L"password_second?", 16) == 0){
//            unsigned int salt_h, salt_l;
//            swscanf(receive_buffer.c_str() + 16, L"%8x%8x", &salt_h, &salt_l);
//            salt = (((unsigned long long) salt_h) << 32) | salt_l;
//            SetEvent(pass_reg_second_request_event);
//          }
//          else if(wcsncmp(receive_buffer.c_str(), L"password?", 9) == 0){
//            unsigned int salt_h, salt_l;
//            swscanf(receive_buffer.c_str() + 9, L"%8x%8x", &salt_h, &salt_l);
//            salt = (((unsigned long long) salt_h) << 32) | salt_l;
//            SetEvent(password_request_event);
//          }
//          else if(wcscmp(receive_buffer.c_str(), L"auth_ok!") == 0){
//            SetEvent(authorized_event);
//          }
//          else if(wcscmp(receive_buffer.c_str(), L"auth_fail!") == 0){
//            SetEvent(auth_fail_event);
//          }
//          else{
//            wchar_t* buf = new wchar_t[receive_buffer.size() + 1];
//            wcscpy(buf, receive_buffer.c_str());
//            win_addwstr_colored(output_win, buf);
//            delete[] buf;
//          }
//            //win_addwstr(output_win, (L"> " + receive_buffer + L"\n").c_str());
//            //win_wprintw(output_win, "> %s\n", receive_buffer.c_str());
//        }
//      }
//    }
//    else{ //disconnected
//      EnterCriticalSection(socket_error_cs);
//      if(WaitForSingleObject(terminate_event, 0) == WAIT_OBJECT_0){
//        if(WaitForSingleObject(socket_error_event, 0) != WAIT_OBJECT_0){
//          win_addwstr(output_win, L"Receive thread terminated.\n");
//          LeaveCriticalSection(socket_error_cs);
//          return 0;
//        }
//      }
//      LeaveCriticalSection(socket_error_cs);
//      Sleep(10);
//    }
//  }
//}
//
//int main()
//{
//  console_init();
//
//  socket_mutex = CreateMutex(NULL, FALSE, NULL);
//
//  DWORD poll;
//  int res;
//  char ip[15];
//  WSADATA data;
//
//  signal(SIGINT, s_handle);
//  signal(SIGKILL, s_handle);
//  signal(SIGQUIT, s_handle);
//
//  //cout << "Enter IP to connect to: ";
//  //gets(ip);
//
//  //if(strlen(ip) == 0)
//
//
//  win_addwstr(output_win, L"WSA Startup... ");
//  res = WSAStartup(MAKEWORD(1, 1), &data);      //Start Winsock
//
////  cout << "\nWSAStartup"
////       << "\nVersion: " << data.wVersion
////       << "\nDescription: " << data.szDescription
////       << "\nStatus: " << data.szSystemStatus << endl;
//
//  if(res != 0)
//      s_cl("failed",WSAGetLastError());
//
//  win_addwstr(output_win, L"OK\n");
//  win_addwstr(output_win, L"Creating socket... ");
//  sock = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);       //Create the socket
//      if(sock == INVALID_SOCKET )
//          s_cl("Invalid Socket", WSAGetLastError());
//      else if(sock == SOCKET_ERROR)
//          s_cl("Socket Error", WSAGetLastError());
//      else
//        win_addwstr(output_win, L"OK\n");
//
//  HANDLE terminate_event = CreateEvent(NULL, TRUE, FALSE, NULL);
//
//  win_addwstr(output_win, L"Enter nickname:\n");
//
//  state = ENTER_NICKNAME;
//
//  HANDLE send_thread, receive_thread;
//  DWORD send_th, receive_th;
//
//  HANDLE thread_handles_ready_event = CreateEvent(NULL, TRUE, FALSE, NULL);
//  //
//  HANDLE connected_event = CreateEvent(NULL, TRUE, FALSE, NULL);
//  HANDLE nickname_request_event = CreateEvent(NULL, FALSE, FALSE, NULL);
//  HANDLE pass_reg_first_request_event = CreateEvent(NULL, FALSE, FALSE, NULL);
//  HANDLE pass_reg_second_request_event = CreateEvent(NULL, FALSE, FALSE, NULL);
//  HANDLE password_request_event = CreateEvent(NULL, FALSE, FALSE, NULL);
//  HANDLE authorized_event = CreateEvent(NULL, TRUE, FALSE, NULL);
//  HANDLE auth_fail_event = CreateEvent(NULL, FALSE, FALSE, NULL);
//  HANDLE recv_error_event = CreateEvent(NULL, TRUE, FALSE, NULL);
//
//  thread_params_t thread_params = {
//    NULL,
//    NULL,
//    sock,
//    thread_handles_ready_event,
//    terminate_event,
//    connected_event,
//    nickname_request_event,
//    pass_reg_first_request_event,
//    pass_reg_second_request_event,
//    password_request_event,
//    authorized_event,
//    auth_fail_event,
//    recv_error_event,
//    input_win,
//    output_win
//  };
//
//  send_thread = CreateThread(NULL, 0, client_to_server, (LPVOID) (&thread_params), 0, &send_th);
//  receive_thread = CreateThread(NULL, 0, server_to_client, (LPVOID) (&thread_params), 0, &receive_th);
//
//  thread_params.send_thread = send_thread;
//  thread_params.receive_thread = receive_thread;
//  SetEvent(thread_handles_ready_event);
//
//  WaitForSingleObject(send_thread, INFINITE);
//  WaitForSingleObject(receive_thread, INFINITE);
//
//  close(sock);
//
//  CloseHandle(thread_handles_ready_event);
//  CloseHandle(terminate_event);
//  CloseHandle(nickname_request_event);
//  CloseHandle(pass_reg_first_request_event);
//  CloseHandle(pass_reg_second_request_event);
//  CloseHandle(password_request_event);
//  CloseHandle(authorized_event);
//  CloseHandle(auth_fail_event);
//
//  CloseHandle(send_thread);
//  CloseHandle(receive_thread);
//
//  closesocket(client);
//  WSACleanup();
//}
