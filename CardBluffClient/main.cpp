#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <winsock.h>
#include <iostream>
#include <fstream>
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
#define NICKNAME_CHARS_NOT_ALLOWED L":%\\"
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
  endwin();
  cout << a;
  s_handle(x + 1000);
}

SOCKET create_socket(){
  output_win_addwstr(L"Creating socket... ");
  SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);       //Create the socket
      if(sock == INVALID_SOCKET )
          s_cl("Invalid Socket", WSAGetLastError());
      else if(sock == (SOCKET) SOCKET_ERROR)
          s_cl("Socket Error", WSAGetLastError());
      else
        output_win_addwstr(L"OK\n");
  return sock;
}

int main(void){
  WSADATA data;
  int res;

  string host_name;
  int port;

  HOSTENT *host;
  //char *host_name;
  sockaddr_in addr;

  console_init();

  ifstream config("config.txt");
  if(config.fail()){
    output_win_addwstr_colored(ERROR_PREFIX L" Can't open configuration file.");
    output_win_addwstr(L"Press any key to exit.\n");
    flushinp();
    while(getch() == ERR){
      Sleep(10);
    }
    s_cl("", 0);
  }

  getline(config, host_name);
  if(config.fail()){
    output_win_addwstr_colored(ERROR_PREFIX L" Can't read server address from configuration file!");
    output_win_addwstr(L"Press any key to exit.\n");
    flushinp();
    while(getch() == ERR){
      Sleep(10);
    }
    s_cl("", 0);
  }

  config >> port;
  if(config.fail()){
    output_win_addwstr_colored(ERROR_PREFIX L" Can't read server port from configuration file!");
    output_win_addwstr(L"Press any key to exit.\n");
    flushinp();
    while(getch() == ERR){
      Sleep(10);
    }
    s_cl("", 0);
  }

  config.close();

  output_win_addwstr(L"WSA Startup... ");
  res = WSAStartup(MAKEWORD(1, 1), &data);      //Start Winsock

  if(res != 0)
      s_cl("failed", WSAGetLastError());

  output_win_addwstr(L"OK\n");

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
            output_win_addwstr_colored(buf);
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
              output_win_addwstr_colored(buf);
              delete[] buf;
            }
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
      output_win_addwstr_colored(ERROR_PREFIX L" Connection is lost!");
      output_win_addwstr(L"Press Enter to try to connect again, or press 'Q' to quit.\n");
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

      wchar_t buf[300];
      swprintf(buf, L"Server address is '%hs', port %u.\n", host_name.c_str(), (unsigned int) port);
      output_win_addwstr(buf);

      output_win_addwstr(L"Retrieving server IP...\n");
      host = gethostbyname(host_name.c_str());
      if(host != NULL){
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = ((in_addr*)host->h_addr_list[0])->s_addr;
        addr.sin_port = htons(port);

        output_win_addwstr((L"Server IP is " + converter.from_bytes(inet_ntoa(addr.sin_addr)) + L"\n").c_str());

        output_win_addwstr(L"Connecting to the server...\n");
        res = connect(socket, (sockaddr*) &addr, sizeof(addr)); //Connect to the server
        if(res == 0){
          output_win_addwstr(L"Connection established!\n");

          unsigned long non_blocking = 1;
          if (ioctlsocket(socket, FIONBIO, &non_blocking) != SOCKET_ERROR){
            output_win_addwstr(L"Connection parameters were set successfully.\n");

            output_win_addwstr(L"Please enter nickname.\n");

            connected = true;
            state = NICKNAME_ENTER;
          }
          else{
            output_win_addwstr_colored(ERROR_PREFIX L" Failed to put socket into non-blocking mode!");
            closesocket(socket);
          }
        }
        else{
          output_win_addwstr_colored(ERROR_PREFIX L" Server unavailable!");
          closesocket(socket);
        }
      }
      else{
        int res = WSAGetLastError();
        if(res == WSAHOST_NOT_FOUND) {
          output_win_addwstr_colored(ERROR_PREFIX L" Host not found!");
        }
        else if(res == WSANO_DATA) {
          output_win_addwstr_colored(ERROR_PREFIX L" No data record found!");
        }
        else{
          output_win_addwstr_colored(ERROR_PREFIX L" Failed!");
        }
      }

      if(!connected){
        output_win_addwstr(L"Press Enter to try to connect again, or press 'Q' to quit.\n");

        state = WAIT_RECONNECT_OR_QUIT;
      }
      break;

    case NICKNAME_ENTER:
      if(!win_get_wstr(input_win, output_win, input_buffer, false, &input_ready)){
        output_win_addwstr_colored(ERROR_PREFIX L" Can't read nickname from console.\n");
      }
      if(!input_ready)
        break;

      if(input_buffer.size() < NICKNAME_SIZE_MIN){
        wchar_t buf[100];
        swprintf(buf, ERROR_PREFIX L" Nickname should contain at least %u characters!", (unsigned int) NICKNAME_SIZE_MIN);
        output_win_addwstr_colored(buf);
        output_win_addwstr(L"Please try again.\n");
        break;
      }
      if(wcscspn(input_buffer.c_str(), NICKNAME_CHARS_NOT_ALLOWED) < input_buffer.size()){
        wchar_t buf[100];
        swprintf(buf, ERROR_PREFIX L" Nickname shouldn't contain these characters: %ls", (unsigned int) NICKNAME_CHARS_NOT_ALLOWED);
        output_win_addwstr_colored(buf);
        output_win_addwstr(L"Please try again.\n");
        break;
      }
      input_buffer = L"nickname:" + input_buffer;
      wcslcpy(send_buffer, input_buffer.c_str(), SEND_BUFFER_SIZE);
      should_send = true;
      state = NICKNAME_SENT;
      break;

    case PASSWORD_REGISTER_FIRST_ENTER:
      if(!win_get_wstr(input_win, output_win, pass, true, &input_ready)){
        output_win_addwstr_colored(ERROR_PREFIX L" Can't read password from console.\n");
      }
      if(!input_ready)
        break;

      if(pass.size() < PASSWORD_SIZE_MIN){
        wchar_t buf[100];
        swprintf(buf, ERROR_PREFIX L" Password should contain at least %u characters!", (unsigned int) PASSWORD_SIZE_MIN);
        output_win_addwstr_colored(buf);
        output_win_addwstr(L"Please try again.\n");
        break;
      }
      wcscpy(send_buffer, L"password_first:");
      wcscat(send_buffer, converter.from_bytes(sha256(converter.to_bytes(pass))).c_str());
      should_send = true;
      state = PASSWORD_REGISTER_FIRST_SENT;
      break;

    case PASSWORD_REGISTER_SECOND_ENTER:
      if(!win_get_wstr(input_win, output_win, pass, true, &input_ready)){
        output_win_addwstr_colored(ERROR_PREFIX L" Can't read password from console.\n");
      }
      if(!input_ready)
        break;

      if(pass.size() < PASSWORD_SIZE_MIN){
        wchar_t buf[100];
        swprintf(buf, ERROR_PREFIX L" Password should contain at least %u characters!", (unsigned int) PASSWORD_SIZE_MIN);
        output_win_addwstr_colored(buf);
        output_win_addwstr(L"Please try again.\n");
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
        output_win_addwstr_colored(ERROR_PREFIX L" Can't read password from console.\n");
      }
      if(!input_ready)
        break;

      if(pass.size() < PASSWORD_SIZE_MIN){
        wchar_t buf[100];
        swprintf(buf, ERROR_PREFIX L" Password should contain at least %u characters!", (unsigned int) PASSWORD_SIZE_MIN);
        output_win_addwstr_colored(buf);
        output_win_addwstr(L"Please try again.\n");
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
        output_win_addwstr_colored(ERROR_PREFIX L" Can't read from console.\n");
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

    case NOT_CONNECTED:
    case NICKNAME_SENT:
    case PASSWORD_REGISTER_FIRST_SENT:
    case PASSWORD_REGISTER_SECOND_SENT:
    case PASSWORD_SENT:
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
