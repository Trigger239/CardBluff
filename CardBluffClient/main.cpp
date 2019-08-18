#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <winsock.h>
#include <iostream>
//#include <conio.h>
#include <signal.h>
#include <cstdio>
//#include <functional>
#include <cctype>
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


DWORD WINAPI client_to_server(LPVOID lpParam){
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
      if(!win_get_wstr(input_win, output_win, input_buffer, false,
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
      if(!win_get_wstr(input_win, output_win, pass, true,
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
      if(!win_get_wstr(input_win, output_win, pass, true,
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
      if(!win_get_wstr(input_win, output_win, pass, true,
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
      if(!win_get_wstr(input_win, output_win, input_buffer, false,
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

int main()
{
  console_init();

  socket_mutex = CreateMutex(NULL, FALSE, NULL);

  DWORD poll;
  int res;
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

  CloseHandle(send_thread);
  CloseHandle(receive_thread);

  closesocket(client);
  WSACleanup();
}
