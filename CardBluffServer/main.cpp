#include <windows.h>
#include <cstdlib>
#include <cstdio>
#include <unordered_set>
#include <winsock2.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <queue>

#include "sqlite/sqlite3.h"
#include "sha256/sha256.h"
#include "client.h"
#include "game.h"
#include "database.h"
#include "util/strlcpy.h"
#include "util/unordered_set_mt.h"
#include "util/Rand64.h"
#include "common.h"
#include "util/unicode.h"

#include <cstdio>

using namespace std;

HANDLE stdout_mutex;

ofstream logfile("CardBluffServer.log");

void log(const char* format, ...){
  va_list args;
  va_start(args, format);
  WaitForSingleObject(stdout_mutex, INFINITE);
  char str[1000];
  vprintf(format, args);
  vsprintf(str, format, args);
  logfile << string(str);
  logfile.flush();
  ReleaseMutex(stdout_mutex);
  va_end(args);
}

struct thread_params_t{
  SOCKET client;
};

unordered_set_mt<Client*> clients;
unordered_set_mt<Game*> games;

DWORD WINAPI game_thread(LPVOID lpParam){
  log("Game thread started\n");
  Game* game = (Game*) lpParam;

  while(true){
    bool _terminate;
    game->process(&_terminate);
    if(_terminate){
      delete game;
      log("Game thread terminated\n");
      return 0;
    }
  }
}

void find_opponent() {
  Client* client1;
  Client* client2;
  bool first_client_found = false;
  clients.lock();
  for(auto it = clients.begin(); it != clients.end(); it++){
      if((*it)->get_finding_duel() && (!(*it)->get_in_game())){
        if(!first_client_found){
          client1 = *it;
          first_client_found = true;
        }
        else{
          client2 = *it;

          client1->set_finding_duel(false);
          client2->set_finding_duel(false);

          client1->push_string(L"SERVER: Your opponent is " + client2->get_nickname());
          client2->push_string(L"SERVER: Your opponent is " + client1->get_nickname());

          Game* game = client1->enter_game(client2);

          log("Duel found: %S VS %S\n", client1->get_nickname().c_str(), client2->get_nickname().c_str());

          clients.unlock();

          DWORD thread;
          game->set_thread(CreateThread(NULL, 0, game_thread, (LPVOID) game, 0, &thread));
          game->start_round();

          return;
        }
      }
    }
  clients.unlock();
}

Client* find_nickname_authorized(wstring& nickname){
  clients.lock();
  for(auto it = clients.begin(); it != clients.end(); it++){
    if((*it)->get_nickname() == nickname && (*it)->get_authorized() && (!(*it)->get_waiting_reconnect())){
        clients.unlock();
        return *it;
      }
  }
  clients.unlock();
  return nullptr;
}

Client* find_nickname_waiting_reconnect(const wstring& nickname){
  clients.lock();
  for(auto it = clients.begin(); it != clients.end(); it++){
    if((*it)->get_nickname() == nickname && (*it)->get_waiting_reconnect()){
        clients.unlock();
        return *it;
      }
  }
  clients.unlock();
  return nullptr;
}


DWORD WINAPI server_to_client(LPVOID lpParam){
  log("Send thread created\n");
  Client* client = (Client*) lpParam;

  int res;
  bool _terminate;

  while(true){
    res = client->send_from_queue(&_terminate);
    if(_terminate || WaitForSingleObject(client->get_terminate_event(), 0) == WAIT_OBJECT_0){
      log("Send thread for client %I64d terminated\n", client->get_id());
      return 0;
    }

    if(res == 0 || res == SOCKET_ERROR){
      HANDLE handles[2] = {client->get_reconnect_event(), client->get_terminate_event()};
      if(WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0 + 1){
        log("Send thread for client %I64d terminated\n", client->get_id());
        return 0;
      }
    }

    Sleep(50);
  }
}

void client_to_server_cleanup(Client* client, sqlite3* db){
  SetEvent(client->get_terminate_event());
  WaitForSingleObject(client->get_send_thread(), INFINITE);
  ResetEvent(client->get_terminate_event());

  if(client->get_state() == IN_GAME){
    Game* game = client->get_game();

    WaitForSingleObject(game->get_thread_handle_ready_event(), INFINITE);
    HANDLE thread = game->get_thread();

    game->finish(client);

    WaitForSingleObject(thread, INFINITE);
  }

  client->close_socket();

  if(!client->get_authorized()){
    if(client->get_state() == WAIT_NICKNAME)
      log("New client: Receive thread terminated\n");
    else
      log("New client '%S': Receive thread terminated\n", client->get_nickname().c_str());
  }
  else
    log("Client %I64d: Receive thread terminated\n", client->get_id());

  clients.erase(client);
  delete client;

  sqlite3_close(db);
}

DWORD WINAPI client_to_server(LPVOID lpParam){
  log("New client: Receive thread created\n");
  Client* client = (Client*) lpParam;

  int res;
  bool _terminate;

  char receive_buffer_raw[RECEIVE_BUFFER_SIZE];

  sqlite3* db;
  if(sqlite3_open("db.sl3", &db)){
    log("New client: Error opening database\n");
    client_to_server_cleanup(client, db);
    return 0;
  };

  Rand64 random;
  long long id_buffer;
  wstring new_password;
  bool skip_processing;

  while(true){
    res = client->receive_data(receive_buffer_raw, RECEIVE_BUFFER_SIZE, &_terminate);
    skip_processing = false;
    if(_terminate || WaitForSingleObject(client->get_terminate_event(), 0) == WAIT_OBJECT_0){
      if(!client->get_authorized()){
        if(client->get_state() == WAIT_NICKNAME)
          log("New client: Receive thread terminated\n");
        else
          log("New client '%S': Receive thread terminated\n", client->get_nickname().c_str());
      }
      else
        log("Client %I64d: Receive thread terminated\n", client->get_id());
      sqlite3_close(db);
      return 0;
    }

    if(res == 0 || res == SOCKET_ERROR){
      if(!client->get_authorized()){
        if(client->get_state() == WAIT_NICKNAME)
          log("New client: Disconnected (receive thread)\n");
        else
          log("New client '%S': Disconnected (receive thread)\n", client->get_nickname().c_str());
        client_to_server_cleanup(client, db);
        return 0;
      }

      ResetEvent(client->get_reconnect_event());
      log("Client %I64d: Disconnected (receive thread)\n", client->get_id());
      client->set_waiting_reconnect(true);

      if(WaitForSingleObject(client->get_reconnect_event(), 60000) == WAIT_OBJECT_0){
        log("Client %I64d: Reconnected\n", client->get_id());
        skip_processing = true;
      }
      else{
        client_to_server_cleanup(client, db);
        return 0;
      }
    }

    if(!skip_processing){
      char* z_err_msg;

      wstring receive_buffer = converter.from_bytes(receive_buffer_raw);
      for(int i = 0; i < receive_buffer.size(); i++){
        log("%u ", (unsigned int) (unsigned char) receive_buffer[i]);
        if(i % 16 == 15)
          log("\n");
      }
      log("receive_buffer_raw = %s\n", receive_buffer_raw);
      log("receive_buffer = %ls\n", receive_buffer.c_str());
      log("receive_buffer.size = %u\n", (unsigned int) receive_buffer.size());
      switch(client->get_state()){
      case WAIT_NICKNAME:
        if(wcsncmp(receive_buffer.c_str(), L"nickname:", 9) == 0){
          log("New client: Nickname message received: '%S'\n", receive_buffer.c_str() + 9);
          wstring nick;
          nick.assign(receive_buffer.c_str() + 9);
          if(find_nickname_authorized(nick) != nullptr){
            log("New client: Client with nickname '%S' already authorized\n", nick.c_str());
            client_to_server_cleanup(client, db);
            return 0;
          }
          client->set_nickname(nick);
          bool exists;
          if(db_get_id_by_nickname(db, client->get_nickname(), &exists, &id_buffer, &z_err_msg)){
            log("New client '%S': Error getting id from database. SQLite error: %s\n",
                client->get_nickname().c_str(), z_err_msg);
            sqlite3_free(z_err_msg);
            client_to_server_cleanup(client, db);
            return 0;
          }
          if(exists){
            client->push_string(&_terminate, L"SERVER: Please enter password.");
            if(_terminate){
              sqlite3_close(db);
              return 0;
            }

            unsigned long long salt_num = random.generate();
            client->push_string(&_terminate, L"password?%08x%08x",
                                (unsigned int) (salt_num >> 32), (unsigned int) (salt_num & 0xFFFFFFFF));
            if(_terminate){
              sqlite3_close(db);
              return 0;
            }
            //client->set_id(id);
            client->set_state(WAIT_PASSWORD);
          }
          else{
            client->push_string(&_terminate, L"SERVER: You are a new user, %S. Please enter your password to register.",
                                client->get_nickname().c_str());
            if(_terminate){
              sqlite3_close(db);
              return 0;
            }
            client->push_string(&_terminate, L"password_first?");
            if(_terminate){
              sqlite3_close(db);
              return 0;
            }
            client->set_state(WAIT_PASSWORD_REGISTER_FIRST);
          }
        }
        else
          log("New client: Wrong nickname message received\n");
        break;

      case WAIT_PASSWORD_REGISTER_FIRST:
        if(wcsncmp(receive_buffer.c_str(), L"password_first:", 15) == 0){
          log("New client '%S': First register password message received: '%S'\n",
              client->get_nickname().c_str(), receive_buffer.c_str() + 15);
          new_password.assign(receive_buffer.c_str() + 15);
          client->push_string(&_terminate, L"SERVER: Please re-enter your password.");
          if(_terminate){
              sqlite3_close(db);
            return 0;
          }
          unsigned long long salt_num = random.generate();
            client->push_string(&_terminate, L"password_second?%08x%08x",
                                (unsigned int) (salt_num >> 32), (unsigned int) (salt_num & 0xFFFFFFFF));
          if(_terminate){
              sqlite3_close(db);
            return 0;
          }
          client->set_state(WAIT_PASSWORD_REGISTER_SECOND);
        }
        else
          log("New client '%S': Wrong first register password message received\n", client->get_nickname().c_str());
        break;

      case WAIT_PASSWORD_REGISTER_SECOND:
        if(wcsncmp(receive_buffer.c_str(), L"password_second:", 16) == 0){
          log("New client '%S': Second register password message received: '%S'\n",
              client->get_nickname().c_str(), receive_buffer.c_str() + 16);
          wstring pass;

          wchar_t salt[17];
          unsigned long long salt_num = random.get_last();
          swprintf(salt, L"%08x%08x", (unsigned int) (salt_num >> 32), (unsigned int) (salt_num & 0xFFFFFFFF));
          pass = new_password + wstring(salt);
          pass = converter.from_bytes(sha256(converter.to_bytes(pass)));

          if(wcscmp(pass.c_str(), receive_buffer.c_str() + 16) == 0){
            log("New client '%S': Second register password OK\n", client->get_nickname().c_str());
            if(db_add_client(db, client->get_nickname(), new_password, &z_err_msg)){
              log("New client '%S': Error adding client to database. SQLite error: %s\n",
                  client->get_nickname().c_str(), z_err_msg);
              sqlite3_free(z_err_msg);
              client_to_server_cleanup(client, db);
              return 0;
            }

            client->push_string(L"auth_ok!", &_terminate);
            if(_terminate){
              sqlite3_close(db);
              return 0;
            }

            client->push_string(L"SERVER: Welcome, " + client->get_nickname() + L"! You are registered now.", &_terminate);
            if(_terminate){
              sqlite3_close(db);
              return 0;
            }
            client->set_authorized(true);

            bool exists;
            if(db_get_id_by_nickname(db, client->get_nickname(), &exists, &id_buffer, &z_err_msg)){
              log("New client '%S': Error getting id from database. SQLite error: %s\n",
                  client->get_nickname().c_str(), z_err_msg);
              sqlite3_free(z_err_msg);
              client_to_server_cleanup(client, db);
              return 0;
            }
            if(!exists){
              log("New client '%S': Newly registered client not found in database\n");
              client_to_server_cleanup(client, db);
              return 0;
            }
            client->set_id(id_buffer);
            client->set_state(WAIT_ENTER_GAME);
          }
          else{
            log("New client '%S': Wrong password\n", client->get_nickname().c_str());
            client->push_string(L"auth_fail!", &_terminate);
            if(_terminate){
              sqlite3_close(db);
              return 0;
            }
            Sleep(1000);
            client_to_server_cleanup(client, db);
            return 0;
          }
        }
        else
          log("New client '%S': Wrong password message received\n", client->get_nickname().c_str());
        break;

      case WAIT_PASSWORD:
        if(wcsncmp(receive_buffer.c_str(), L"password:", 9) == 0){
          log("New client '%S': Password message received: '%S'\n",
              client->get_nickname().c_str(), receive_buffer.c_str() + 9);
          wstring pass;
          if(db_get_password(db, id_buffer, &pass, &z_err_msg)){
            log("New client '%S': Error getting password from database. SQLite error: %s\n",
                client->get_nickname().c_str(), z_err_msg);
            sqlite3_free(z_err_msg);
            client_to_server_cleanup(client, db);
            return 0;
          }

          log("Pass from database: '%S'\n", pass.c_str());

          wchar_t salt[17];
          unsigned long long salt_num = random.get_last();
          swprintf(salt, L"%08x%08x", (unsigned int) (salt_num >> 32), (unsigned int) (salt_num & 0xFFFFFFFF));
          pass += wstring(salt);
          pass = converter.from_bytes(sha256(converter.to_bytes(pass)));
          log("Waiting pass: '%S' (salt '%S')\n", pass.c_str(), salt);

          if(wcscmp(pass.c_str(), receive_buffer.c_str() + 9) == 0){
            log("New client '%S': Password OK\n", client->get_nickname().c_str());

            client->push_string(L"auth_ok!", &_terminate);
            if(_terminate){
              sqlite3_close(db);
              return 0;
            }

            client->push_string(L"SERVER: Welcome, " + client->get_nickname() + L"!", &_terminate);
            if(_terminate){
              sqlite3_close(db);
              return 0;
            }
            client->set_authorized(true);

            Client* old_client = find_nickname_waiting_reconnect(client->get_nickname());
            if(old_client != nullptr){
              log("New client '%S': Reconnected client %I64d\n", client->get_nickname().c_str(), old_client->get_id());
              SetEvent(client->get_terminate_event());
              WaitForSingleObject(client->get_send_thread(), INFINITE);
              ResetEvent(client->get_terminate_event());

              old_client->set_socket(client->get_socket());
              client->copy_strings(old_client);
              SetEvent(old_client->get_reconnect_event());
              old_client->set_waiting_reconnect(false);

              log("New client '%S': Receive thread terminated\n", client->get_nickname().c_str());

              clients.erase(client);
              delete client;

              sqlite3_close(db);

              return 0;
            }

            client->set_id(id_buffer);
            client->set_state(WAIT_ENTER_GAME);
          }
          else{
            log("New client '%S': Wrong password\n", client->get_nickname().c_str());
            client->push_string(L"auth_fail!", &_terminate);
            if(_terminate){
              sqlite3_close(db);
              return 0;
            }
            client_to_server_cleanup(client, db);
            return 0;
          }
        }
        else
          log("New client '%S': Wrong password message received\n", client->get_nickname().c_str());
        break;

      case WAIT_ENTER_GAME:
        if(wcsncmp(receive_buffer.c_str(), L"/", 1) != 0){
          client->push_string(&_terminate, L"SERVER: You should find opponent to use chat.");
          if(_terminate){
            log("Client %I64d: Receive thread terminated\n", client->get_id());
            sqlite3_close(db);
            return 0;
          }
        }
        else if(wcscmp(receive_buffer.c_str(), L"/findduel") == 0){
          client->set_finding_duel(true);
          client->push_string(&_terminate, L"SERVER: Finding opponent for you...");
          if(_terminate){
            log("Client %I64d: Receive thread terminated\n", client->get_id());
            sqlite3_close(db);
            return 0;
          }
          client->set_state(WAIT_OPPONENT);
        }
        else{
          client->push_string(&_terminate, L"SERVER: Invalid command!");
          if(_terminate){
            log("Client %I64d: Receive thread terminated\n", client->get_id());
            sqlite3_close(db);
            return 0;
          }
        }
        break;

      case WAIT_OPPONENT:
        break;

      case IN_GAME:
        if(wcsncmp(receive_buffer.c_str(), L"/", 1) != 0){ //not a command
          client->get_opponent()->push_string(&_terminate, L"%S: %S", client->get_nickname().c_str(), receive_buffer.c_str());
          if(_terminate){
            log("Client %I64d: Receive thread terminated\n", client->get_id());
            sqlite3_close(db);
            return 0;
          }
        }
        else if(wcscmp(receive_buffer.c_str(), L"/help") == 0){
          client->push_string(&_terminate,
                              L"SERVER: Command '/help' is not supported yet.");
          if(_terminate){
            log("Client %I64d: Receive thread terminated\n", client->get_id());
            sqlite3_close(db);
            return 0;
          }
        }
        //TODO: process other non-game commands like /top
        else{ //game command
          Game* game = client->get_game();
          game->make_move(client, receive_buffer, &_terminate);
          if(_terminate){
            log("Client %I64d: Receive thread terminated\n", client->get_id());
            sqlite3_close(db);
            return 0;
          }
        }
        break;
      }
    }
  }
}

DWORD WINAPI find_duel_thread(LPVOID lpParam){
  log("Opponent finding thread started\n");
  while(true){
    find_opponent();
    Sleep(100);
  }
}

int main()
{
  std::wstring_convert<std::codecvt_utf8<wchar_t>> converter_;

  char* z_err_msg;
  setlocale(LC_ALL, "ru_RU.utf8");
  //SetConsoleCP(65001);

  wstring s(L"ab");
  char str_raw[30];
  string st = converter_.to_bytes(s);
  wstring wstr = converter_.from_bytes(st);
  wcout << L"1.5" << s << ' ' << wstr << L'\n';
  copy(st.begin(), st.end() + 1, str_raw);
  cout << '\"' << st << '\"' << ' ' << st.size() << '\n';
  log("1: %s, %u\n", str_raw, strlen(str_raw));
  for(int i = 0; i < strlen(str_raw) + 1; i++)
    log("%u ", (unsigned int) (unsigned char) str_raw[i]);
  log("\n");
  log("2: %ls\n", wstr.c_str());

  if(!sqlite3_threadsafe()){
    log("Current SQLite version is not thread safe.\n");
    return 0;
  };

  sqlite3* db;
  if(sqlite3_open("db.sl3", &db)){
    log("Error opening database.\n");
    sqlite3_close(db);
    return 0;
  };

  if(sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS clients(id INTEGER PRIMARY KEY,nickname TEXT UNIQUE,password TEXT,rating INTEGER);", NULL, nullptr, &z_err_msg)){
    log("Error creating table or checking its existance.\nSQLite error: %s\n", z_err_msg);
    sqlite3_free(z_err_msg);
    sqlite3_close(db);
    return 0;
  }

  SOCKET main_socket;
  DWORD thread;

  WSADATA wsaData;

  int ret = WSAStartup(0x101, &wsaData); // use highest version of winsock avalible

  if(ret != 0){
    return 0;
  }

  sockaddr_in server;

  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(PORT);

  main_socket = socket(AF_INET, SOCK_STREAM, 0);

  if(main_socket == INVALID_SOCKET){
    return 0;
  }

  if(bind(main_socket, (sockaddr*)&server, sizeof(server)) !=0){
    return 0;
  }

  if(listen(main_socket, MAX_CLIENTS) != 0){
    return 0;
  }

  SOCKET client_socket;

  sockaddr_in from;
  int fromlen = sizeof(from);

  stdout_mutex = CreateMutex(NULL, FALSE, NULL);

  CreateThread(NULL, 0, find_duel_thread, (LPVOID) 0, 0, &thread);

  while(true){
    log("Waiting for client\n");
    client_socket = accept(main_socket, (struct sockaddr*) &from, &fromlen);
    log("Client connected\n");

    unsigned long non_blocking = 1;
    if (ioctlsocket(client_socket, FIONBIO, &non_blocking) == SOCKET_ERROR) {
      log("Failed to put socket into non-blocking mode\n");
      closesocket(client_socket);
      continue;
    }

    Client* client = new Client(client_socket);
    clients.insert(client);

    client->set_send_thread(CreateThread(NULL, 0, server_to_client, (LPVOID) client, 0, &thread));
    client->set_receive_thread(CreateThread(NULL, 0, client_to_server, (LPVOID) client, 0, &thread));
  }

  closesocket(main_socket);
  WSACleanup();

  return 0;
}
