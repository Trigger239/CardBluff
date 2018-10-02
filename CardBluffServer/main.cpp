#include <windows.h>
#include <cstdlib>
#include <cstdio>
#include <unordered_set>
#include <winsock2.h>
#include <iostream>
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

using namespace std;

HANDLE stdout_mutex;

void log(const char* format, ...){
  va_list args;
  va_start(args, format);
  WaitForSingleObject(stdout_mutex, INFINITE);
  vprintf(format, args);
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

          client1->push_string("SERVER: Your opponent is " + client2->get_nickname());
          client2->push_string("SERVER: Your opponent is " + client1->get_nickname());

          Game* game = client1->enter_game(client2);

          log("Duel found: %s VS %s\n", client1->get_nickname().c_str(), client2->get_nickname().c_str());

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

Client* find_nickname_authorized(string& nickname){
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

Client* find_nickname_waiting_reconnect(const string& nickname){
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
      log("New client '%s': Receive thread terminated\n", client->get_nickname().c_str());
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

  char receive_buffer[RECEIVE_BUFFER_SIZE];

  sqlite3* db;
  if(sqlite3_open("db.sl3", &db)){
    log("New client: Error opening database\n");
    client_to_server_cleanup(client, db);
    return 0;
  };

  Rand64 random;
  long long id_buffer;
  string new_password;
  bool skip_processing;

  while(true){
    res = client->receive_data(receive_buffer, RECEIVE_BUFFER_SIZE, &_terminate);
    skip_processing = false;
    if(_terminate || WaitForSingleObject(client->get_terminate_event(), 0) == WAIT_OBJECT_0){
      if(!client->get_authorized()){
        if(client->get_state() == WAIT_NICKNAME)
          log("New client: Receive thread terminated\n");
        else
          log("New client '%s': Receive thread terminated\n", client->get_nickname().c_str());
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
          log("New client '%s': Disconnected (receive thread)\n", client->get_nickname().c_str());
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

      switch(client->get_state()){
      case WAIT_NICKNAME:
        if(strncmp(receive_buffer, "nickname:", 9) == 0){
          log("New client: Nickname message received: '%s'\n", receive_buffer + 9);
          string nick;
          nick.assign(receive_buffer + 9);
          if(find_nickname_authorized(nick) != nullptr){
            log("New client: Client with nickname '%s' already authorized\n", nick.c_str());
            client_to_server_cleanup(client, db);
            return 0;
          }
          client->set_nickname(nick);
          bool exists;
          if(db_get_id_by_nickname(db, client->get_nickname(), &exists, &id_buffer, &z_err_msg)){
            log("New client '%s': Error getting id from database. SQLite error: %s\n",
                client->get_nickname().c_str(), z_err_msg);
            sqlite3_free(z_err_msg);
            client_to_server_cleanup(client, db);
            return 0;
          }
          if(exists){
            client->push_string(&_terminate, "SERVER: Please enter password.",
                                client->get_nickname().c_str());
            if(_terminate){
              sqlite3_close(db);
              return 0;
            }

            unsigned long long salt_num = random.generate();
            client->push_string(&_terminate, "password?%08x%08x",
                                (unsigned int) (salt_num >> 32), (unsigned int) (salt_num & 0xFFFFFFFF));
            if(_terminate){
              sqlite3_close(db);
              return 0;
            }
            //client->set_id(id);
            client->set_state(WAIT_PASSWORD);
          }
          else{
            client->push_string(&_terminate, "SERVER: You are a new user, %s. Please enter your password to register.",
                                client->get_nickname().c_str());
            if(_terminate){
              sqlite3_close(db);
              return 0;
            }
            client->push_string(&_terminate, "password_first?");
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
        if(strncmp(receive_buffer, "password_first:", 15) == 0){
          log("New client '%s': First register password message received: '%s'\n",
              client->get_nickname().c_str(), receive_buffer + 15);
          new_password.assign(receive_buffer + 15);
          client->push_string(&_terminate, "SERVER: Please re-enter your password.",
                              client->get_nickname().c_str());
          if(_terminate){
              sqlite3_close(db);
            return 0;
          }
          unsigned long long salt_num = random.generate();
            client->push_string(&_terminate, "password_second?%08x%08x",
                                (unsigned int) (salt_num >> 32), (unsigned int) (salt_num & 0xFFFFFFFF));
          if(_terminate){
              sqlite3_close(db);
            return 0;
          }
          client->set_state(WAIT_PASSWORD_REGISTER_SECOND);
        }
        else
          log("New client '%s': Wrong first register password message received\n", client->get_nickname().c_str());
        break;

      case WAIT_PASSWORD_REGISTER_SECOND:
        if(strncmp(receive_buffer, "password_second:", 16) == 0){
          log("New client '%s': Second register password message received: '%s'\n",
              client->get_nickname().c_str(), receive_buffer + 16);
          string pass;

          char salt[17];
          unsigned long long salt_num = random.get_last();
          sprintf(salt, "%08x%08x", (unsigned int) (salt_num >> 32), (unsigned int) (salt_num & 0xFFFFFFFF));
          pass = new_password + string(salt);
          pass = sha256(pass);

          if(strcmp(pass.c_str(), receive_buffer + 16) == 0){
            log("New client '%s': Second register password OK\n", client->get_nickname().c_str());
            if(db_add_client(db, client->get_nickname(), new_password, &z_err_msg)){
              log("New client '%s': Error adding client to database. SQLite error: %s\n",
                  client->get_nickname().c_str(), z_err_msg);
              sqlite3_free(z_err_msg);
              client_to_server_cleanup(client, db);
              return 0;
            }

            client->push_string("auth_ok!", &_terminate);
            if(_terminate){
              sqlite3_close(db);
              return 0;
            }

            client->push_string("SERVER: Welcome, " + client->get_nickname() + "! You are registered now.", &_terminate);
            if(_terminate){
              sqlite3_close(db);
              return 0;
            }
            client->set_authorized(true);

            bool exists;
            if(db_get_id_by_nickname(db, client->get_nickname(), &exists, &id_buffer, &z_err_msg)){
              log("New client '%s': Error getting id from database. SQLite error: %s\n",
                  client->get_nickname().c_str(), z_err_msg);
              sqlite3_free(z_err_msg);
              client_to_server_cleanup(client, db);
              return 0;
            }
            if(!exists){
              log("New client '%s': Newly registered client not found in database\n");
              client_to_server_cleanup(client, db);
              return 0;
            }
            client->set_id(id_buffer);
            client->set_state(WAIT_ENTER_GAME);
          }
          else{
            log("New client '%s': Wrong password\n", client->get_nickname().c_str());
            client->push_string("auth_fail!", &_terminate);
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
          log("New client '%s': Wrong password message received\n", client->get_nickname().c_str());
        break;

      case WAIT_PASSWORD:
        if(strncmp(receive_buffer, "password:", 9) == 0){
          log("New client '%s': Password message received: '%s'\n",
              client->get_nickname().c_str(), receive_buffer + 9);
          string pass;
          if(db_get_password(db, id_buffer, &pass, &z_err_msg)){
            log("New client '%s': Error getting password from database. SQLite error: %s\n",
                client->get_nickname().c_str(), z_err_msg);
            sqlite3_free(z_err_msg);
            client_to_server_cleanup(client, db);
            return 0;
          }

          log("Pass from database: '%s'\n", pass.c_str());

          char salt[17];
          unsigned long long salt_num = random.get_last();
          sprintf(salt, "%08x%08x", (unsigned int) (salt_num >> 32), (unsigned int) (salt_num & 0xFFFFFFFF));
          pass += string(salt);
          pass = sha256(pass);
          log("Waiting pass: '%s' (salt '%s')\n", pass.c_str(), salt);

          if(strcmp(pass.c_str(), receive_buffer + 9) == 0){
            log("New client '%s': Password OK\n", client->get_nickname().c_str());

            client->push_string("auth_ok!", &_terminate);
            if(_terminate){
              sqlite3_close(db);
              return 0;
            }

            client->push_string("SERVER: Welcome, " + client->get_nickname() + "!", &_terminate);
            if(_terminate){
              sqlite3_close(db);
              return 0;
            }
            client->set_authorized(true);

            Client* old_client = find_nickname_waiting_reconnect(client->get_nickname());
            if(old_client != nullptr){
              log("New client '%s': Reconnected client %I64d\n", client->get_nickname().c_str(), old_client->get_id());
              SetEvent(client->get_terminate_event());
              WaitForSingleObject(client->get_send_thread(), INFINITE);
              ResetEvent(client->get_terminate_event());

              old_client->set_socket(client->get_socket());
              client->copy_strings(old_client);
              SetEvent(old_client->get_reconnect_event());
              old_client->set_waiting_reconnect(false);

              log("New client '%s': Receive thread terminated\n", client->get_nickname().c_str());

              clients.erase(client);
              delete client;

              sqlite3_close(db);

              return 0;
            }

            client->set_id(id_buffer);
            client->set_state(WAIT_ENTER_GAME);
          }
          else{
            log("New client '%s': Wrong password\n", client->get_nickname().c_str());
            client->push_string("auth_fail!", &_terminate);
            if(_terminate){
              sqlite3_close(db);
              return 0;
            }
            client_to_server_cleanup(client, db);
            return 0;
          }
        }
        else
          log("New client '%s': Wrong password message received\n", client->get_nickname().c_str());
        break;

      case WAIT_ENTER_GAME:
        if(strncmp(receive_buffer, "/", 1) != 0){
          client->push_string(&_terminate, "SERVER: You should find opponent to use chat.");
          if(_terminate){
            log("Client %I64d: Receive thread terminated\n", client->get_id());
            sqlite3_close(db);
            return 0;
          }
        }
        else if(strcmp(receive_buffer, "/find_duel") == 0){
          client->set_finding_duel(true);
          client->push_string(&_terminate, "SERVER: Finding opponent for you...");
          if(_terminate){
            log("Client %I64d: Receive thread terminated\n", client->get_id());
            sqlite3_close(db);
            return 0;
          }
          client->set_state(WAIT_OPPONENT);
        }
        else{
          client->push_string(&_terminate, "SERVER: Invalid command!");
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
        if(strncmp(receive_buffer, "/", 1) != 0){ //not a command
          client->get_opponent()->push_string(&_terminate, "%s: %s", client->get_nickname().c_str(), receive_buffer);
          if(_terminate){
            log("Client %I64d: Receive thread terminated\n", client->get_id());
            sqlite3_close(db);
            return 0;
          }
        }
        else if(strcmp(receive_buffer, "/help") == 0){
          client->push_string(&_terminate,
                              "SERVER: Command '/help' is not supported yet.");
          if(_terminate){
            log("Client %I64d: Receive thread terminated\n", client->get_id());
            sqlite3_close(db);
            return 0;
          }
        }
        //TODO: process other non-game commands like /top
        else{ //game command
          Game* game = client->get_game();
          game->make_move(client, string(receive_buffer), &_terminate);
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
  char* z_err_msg;

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
