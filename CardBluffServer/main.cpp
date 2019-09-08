#define _WIN32_WINNT 0x0601
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
#include "util/logger.h"

#include <cstdio>
#include <io.h>
#include <fcntl.h>

const char db_filename[] = "db.sl3";

#ifdef SET_CONSOLE_FONT
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

#endif

using namespace std;

struct thread_params_t{
  SOCKET client;
};

unordered_set_mt<Client*> clients;
unordered_set_mt<Game*> games;

DWORD WINAPI game_thread(LPVOID lpParam){
  Game* game = (Game*) lpParam;

  Logger& logger = game->get_logger();
  logger(L"Game thread started");

  sqlite3* db;
  logger << L"Opening database '" << db_filename << "'..." << Logger::endline;
  if(sqlite3_open(db_filename, &db)){
    logger << L"Error opening database '" << db_filename << L"'" << Logger::endline;
    logger(L"Closing database...");
    sqlite3_close(db);
    logger(L"Game thread terminated");
    return 0;
  };

  game->set_db(db);

  game->start_round();

  while(true){
    bool _terminate;
    game->process(&_terminate);
    if(_terminate){
      break;
    }
  }

  logger(L"Closing database...");
  sqlite3_close(db);

  logger(L"Game thread terminated");

  delete game;

  return 0;
}

void find_opponent(Logger& logger) {
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

          client1->push_string(SERVER_PREFIX L" Your opponent is " + client2->get_nickname_with_color());
          client2->push_string(SERVER_PREFIX L" Your opponent is " + client1->get_nickname_with_color());

          Game* game = client1->enter_game(client2);

          logger << L"Duel found: '" << client1->get_nickname() <<
                    L"' VS '" << client2->get_nickname() << L"'\n" <<
                    L"Creating game thread..." << Logger::endline;

          clients.unlock();

          DWORD thread;
          game->set_thread(CreateThread(NULL, 0, game_thread, (LPVOID) game, 0, &thread));
          SetEvent(game->get_thread_handle_ready_event());

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
  Client* client = (Client*) lpParam;

  Logger logger(L"Client " + ptr_to_wstring(client) + L" (send)");
  logger(L"Send thread created");

  int res;
  bool prefix_set_nick = false;
  bool prefix_set_auth = false;

  while(true){
    if(WaitForSingleObject(client->get_terminate_event(), 0) == WAIT_OBJECT_0){
      logger(L"Terminate signal received");
      logger(L"Send thread terminated");
      return 0;
    }

    res = client->send_from_queue();
    if(res == 0 || res == SOCKET_ERROR || res == -239){
      if(res == SOCKET_ERROR){
        if(WSAGetLastError() == WSAEWOULDBLOCK){
          Sleep(10);
          continue;
        }
      }
      if(res == -239){
        Sleep(10);
        continue;
      }
      HANDLE handles[2] = {client->get_reconnect_event(), client->get_terminate_event()};
      if(WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0 + 1){
        logger(L"Terminate signal received");
        logger(L"Send thread terminated");
        return 0;
      }
    }

    if(!prefix_set_nick && client->get_nickname() != L""){
      logger.set_prefix(L"Client '" + client->get_nickname() + L"'* (send)");
      prefix_set_nick = true;
    }
    if(prefix_set_nick && !prefix_set_auth){
      logger.set_prefix(L"Client '" + client->get_nickname() + L"' (send)");
      prefix_set_auth = true;
    }
  }
}

void client_to_server_cleanup(Client* client, sqlite3* db, Logger& logger){
  logger(L"Cleanup...");
  SetEvent(client->get_terminate_event());
  logger(L"Waiting for send thread to terminate...");
  WaitForSingleObject(client->get_send_thread(), INFINITE);
  ResetEvent(client->get_terminate_event());

  if(client->get_state() == IN_GAME){
    Game* game = client->get_game();

    logger(L"Game in progress, waiting in to finish...");
    WaitForSingleObject(game->get_thread_handle_ready_event(), INFINITE);
    HANDLE thread = game->get_thread();

    //Waiting game thread to terminate
    WaitForSingleObject(thread, INFINITE);
  }

  logger(L"Closing socket...");
  client->close_socket();

  clients.erase(client);
  delete client;

  logger(L"Closing database...");
  sqlite3_close(db);

  logger(L"Receive thread terminated");
}

DWORD WINAPI client_to_server(LPVOID lpParam){
  Client* client = (Client*) lpParam;

  Logger logger(L"Client " + ptr_to_wstring(client) + L" (recv)");
  logger(L"Receive thread created");

  int res;

  char receive_buffer_raw[RECEIVE_BUFFER_SIZE];

  sqlite3* db;
  logger << L"Opening database '" << db_filename << "'..." << Logger::endline;
  if(sqlite3_open(db_filename, &db)){
    logger << L"Error opening database '" << db_filename << L"'" << Logger::endline;
    client_to_server_cleanup(client, db, logger);
    return 0;
  };
  logger << L"Database file '" << db_filename << L"' opened" << Logger::endline;

  Rand64 random;
  long long id_buffer;
  wstring new_password;
  bool skip_processing;

  while(true){
    if(WaitForSingleObject(client->get_terminate_event(), 0) == WAIT_OBJECT_0){
      logger(L"Terminate signal received");
      client_to_server_cleanup(client, db, logger);
      return 0;
    }

    res = client->receive_data(receive_buffer_raw, RECEIVE_BUFFER_SIZE);
    if(res == SOCKET_ERROR){
      if(WSAGetLastError() == WSAEWOULDBLOCK){
        Sleep(10);
        continue;
      }
    }

    skip_processing = false;

    if(res == 0 || res == SOCKET_ERROR){
      if(!client->get_authorized()){
        client_to_server_cleanup(client, db, logger);
        return 0;
      }

      ResetEvent(client->get_reconnect_event());
      logger(L"Disconnected (receive thread)");
      client->set_waiting_reconnect(true);
      logger(L"Waiting reconnect...");

      if(WaitForSingleObject(client->get_reconnect_event(), RECONNECT_TIMEOUT) == WAIT_OBJECT_0){
        logger(L"Reconnected");
        skip_processing = true;
      }
      else{
        logger(L"Reconnect timeout ended");
        if(client->get_in_game()){
          client->get_game()->push_disconnect(client);
        }
        client_to_server_cleanup(client, db, logger);
        return 0;
      }
    }

    if(!skip_processing){
      char* z_err_msg;

      wstring receive_buffer = converter.from_bytes(receive_buffer_raw);
      switch(client->get_state()){
      case WAIT_NICKNAME:
        if(wcsncmp(receive_buffer.c_str(), L"nickname:", 9) == 0){
          logger << L"Nickname received: " << (receive_buffer.c_str() + 9) << Logger::endline;
          wstring nick;
          nick.assign(receive_buffer.c_str() + 9);
          if(find_nickname_authorized(nick) != nullptr){
            logger << L"Client with nickname '" << (receive_buffer.c_str() + 9) << "' was already authorized!" << Logger::endline;
            client_to_server_cleanup(client, db, logger);
            return 0;
          }
          client->set_nickname(nick);
          logger.set_prefix(L"Client '" + wstring(receive_buffer.c_str() + 9) + L"'* (recv)");
          logger(L"Checking if this nickname exists in database...");
          bool exists;
          if(db_get_id_by_nickname(db, client->get_nickname(), &exists, &id_buffer, &z_err_msg)){
            logger << L"Error getting id from database: " << z_err_msg << Logger::endline;
            sqlite3_free(z_err_msg);
            client_to_server_cleanup(client, db, logger);
            return 0;
          }
          if(exists){
            logger << L"id found in database: " << id_buffer << Logger::endline;
            logger(L"Sending password request...");
            client->push_string(SERVER_PREFIX L" Please enter password.");

            unsigned long long salt_num = random.generate();
            client->push_string_format(L"password?%08x%08x",
                                (unsigned int) (salt_num >> 32), (unsigned int) (salt_num & 0xFFFFFFFF));
            //client->set_id(id);
            client->set_state(WAIT_PASSWORD);
            logger(L"Waiting password hash...");
          }
          else{
            logger(L"Nickname not found in database. This is a new user");
            logger(L"Sending first password request...");
            client->push_string_format(SERVER_PREFIX L" You are a new user, %ls. Please enter your password to register.",
                                client->get_nickname_with_color().c_str());
            client->push_string(L"password_first?");
            client->set_state(WAIT_PASSWORD_REGISTER_FIRST);
            logger(L"Waiting first register password hash...");
          }
        }
        else
          logger(L"Wrong nickname message received");
        break;

      case WAIT_PASSWORD_REGISTER_FIRST:
        if(wcsncmp(receive_buffer.c_str(), L"password_first:", 15) == 0){
          logger << L"First register password hash received: '" << (receive_buffer.c_str() + 15) << L"'" << Logger::endline;

          new_password.assign(receive_buffer.c_str() + 15);
          logger(L"Sending second register password request...");
          client->push_string(SERVER_PREFIX L" Please re-enter your password.");
          unsigned long long salt_num = random.generate();
            client->push_string_format(L"password_second?%08x%08x",
                                (unsigned int) (salt_num >> 32), (unsigned int) (salt_num & 0xFFFFFFFF));
          logger(L"Waiting second register password hash ...");
          client->set_state(WAIT_PASSWORD_REGISTER_SECOND);
        }
        else
          logger(L"Wrong first register password message received");
        break;

      case WAIT_PASSWORD_REGISTER_SECOND:
        if(wcsncmp(receive_buffer.c_str(), L"password_second:", 16) == 0){
          logger << L"Second register password hash received: '" << (receive_buffer.c_str() + 16) << L"'" << Logger::endline;
          wstring pass;

          wchar_t salt[17];
          unsigned long long salt_num = random.get_last();
          swprintf(salt, L"%08x%08x", (unsigned int) (salt_num >> 32), (unsigned int) (salt_num & 0xFFFFFFFF));
          logger << L"Salt value is '" << salt << L"'" << Logger::endline;

          pass = new_password + wstring(salt);
          pass = converter.from_bytes(sha256(converter.to_bytes(pass)));

          logger << L"Waiting password hash '" << pass << L"'" << Logger::endline;
          if(wcscmp(pass.c_str(), receive_buffer.c_str() + 16) == 0){
            logger(L"OK! Register password hashes match!");

            if(db_add_client(db, client->get_nickname(), new_password, &z_err_msg)){
              logger << L"Error adding client to database: " << z_err_msg << Logger::endline;
              sqlite3_free(z_err_msg);
              client_to_server_cleanup(client, db, logger);
              return 0;
            }

            logger(L"Authorized!");
            logger.set_prefix(L"Client '" + client->get_nickname() + L"' (recv)");

            client->push_string(L"auth_ok!");
            client->push_string(SERVER_PREFIX L" Welcome, " + client->get_nickname_with_color() + L"! You are registered now.");
            client->set_authorized(true);

            logger(L"Getting id from database...");
            bool exists;
            if(db_get_id_by_nickname(db, client->get_nickname(), &exists, &id_buffer, &z_err_msg)){
              logger << L"Error getting id from database: " << z_err_msg << Logger::endline;
              sqlite3_free(z_err_msg);
              client_to_server_cleanup(client, db, logger);
              return 0;
            }
            if(!exists){
              logger(L"Error: id not found in database, but we added it there a moment ago!");
              client_to_server_cleanup(client, db, logger);
              return 0;
            }
            logger << L"id found in database: " << id_buffer << Logger::endline;
            client->set_id(id_buffer);
            logger(L"Waiting entering game...");
            client->set_state(WAIT_ENTER_GAME);
          }
          else{
            logger(L"Error: password hashes don't match!");
            client->push_string(L"auth_fail!");
            Sleep(1000);
            client_to_server_cleanup(client, db, logger);
            return 0;
          }
        }
        else
          logger(L"Wrong second register password message received");
        break;

      case WAIT_PASSWORD:
        if(wcsncmp(receive_buffer.c_str(), L"password:", 9) == 0){
          logger << L"Password hash received: '" << (receive_buffer.c_str() + 9) << L"'" << Logger::endline;
          wstring pass;
          if(db_get_password(db, id_buffer, &pass, &z_err_msg)){
            logger << L"Error getting password from database: " << z_err_msg << Logger::endline;
            sqlite3_free(z_err_msg);
            client_to_server_cleanup(client, db, logger);
            return 0;
          }

          logger << L"Password hash from database: '" << pass << L"'" << Logger::endline;

          wchar_t salt[17];
          unsigned long long salt_num = random.get_last();
          swprintf(salt, L"%08x%08x", (unsigned int) (salt_num >> 32), (unsigned int) (salt_num & 0xFFFFFFFF));
          logger << L"Salt value is '" << salt << L"'" << Logger::endline;

          pass += wstring(salt);
          pass = converter.from_bytes(sha256(converter.to_bytes(pass)));

          logger << L"Waiting password hash '" << pass << L"'" << Logger::endline;

          if(wcscmp(pass.c_str(), receive_buffer.c_str() + 9) == 0){
            logger(L"OK! Password hashes match!");

            logger(L"Authorized!");

            client->push_string(L"auth_ok!");
            client->push_string(SERVER_PREFIX L" Welcome, " + client->get_nickname_with_color() + L"!");

            client->set_authorized(true);

            Client* old_client = find_nickname_waiting_reconnect(client->get_nickname());
            if(old_client != nullptr){
              logger << L"Reconnected client " << ptr_to_wstring(old_client) << Logger::endline;
              SetEvent(client->get_terminate_event());
              logger.set_prefix(L"Client '" + client->get_nickname() + L"' (temporary)");
              WaitForSingleObject(client->get_send_thread(), INFINITE);
              ResetEvent(client->get_terminate_event());

              old_client->set_socket(client->get_socket());
              client->copy_strings(old_client);
              SetEvent(old_client->get_reconnect_event());
              old_client->set_waiting_reconnect(false);

              clients.erase(client);
              delete client;

              logger(L"Closing database...");
              sqlite3_close(db);

              logger(L"Receive thread terminated");

              return 0;
            }

            client->set_id(id_buffer);

            logger.set_prefix(L"Client '" + client->get_nickname() + L"' (recv)");
            logger(L"Waiting entering game...");

            client->set_state(WAIT_ENTER_GAME);
          }
          else{
            logger(L"Error: password hashes don't match!");
            client->push_string(L"auth_fail!");
            Sleep(1000);
            client_to_server_cleanup(client, db, logger);
            return 0;
          }
        }
        else
          logger(L"Wrong password message received");
        break;

      case WAIT_ENTER_GAME:
        if(wcsncmp(receive_buffer.c_str(), L"/", 1) != 0){
          client->push_string(SERVER_PREFIX L" You should find an opponent to use chat.");
        }
        else if(wcscmp(receive_buffer.c_str(), L"/findduel") == 0 || wcscmp(receive_buffer.c_str(), L"/fd") == 0){
          logger(L"Finding opponent...");
          client->set_finding_duel(true);
          client->push_string(SERVER_PREFIX L" Finding an opponent for you...");
          client->set_state(WAIT_OPPONENT);
        }
        else{
          client->push_string(SERVER_PREFIX L" Invalid command!");
        }
        break;

      case WAIT_OPPONENT:
        if(wcsncmp(receive_buffer.c_str(), L"/", 1) != 0){
          client->push_string(SERVER_PREFIX L" You should find an opponent to use chat.");
        }
        else if(remove_space_characters(receive_buffer) == L"/cancel"){
          logger(L"Opponent finding canceled");
          client->set_finding_duel(false);
          client->push_string(SERVER_PREFIX L" Duel finding canceled.");
          client->set_state(WAIT_ENTER_GAME);
        }
        break;

      case IN_GAME:
        if(wcsncmp(remove_space_characters(receive_buffer).c_str(), L"/", 1) != 0){ //not a command
          client->get_opponent()->push_string(USER_PREFIX + client->get_nickname_with_color() + L": " +  receive_buffer);
        }
        else if(wcscmp(receive_buffer.c_str(), L"/help") == 0){
          client->push_string(SERVER_PREFIX L" Command '/help' is not supported yet.");
        }
        //TODO: process other non-game commands like /top
        else{ //game command
          Game* game = client->get_game();
          //game->make_move(client, receive_buffer);
          game->push_command(client, receive_buffer);
        }
        break;
      }
    }
  }
}

DWORD WINAPI find_duel_thread(LPVOID lpParam){
  Logger logger(L"Find Duel");
  logger(L"Opponent finding thread started");
  while(true){
    find_opponent(logger);
    Sleep(100);
  }
}

bool set_console_font(const wchar_t* font)
{
#ifdef SET_CONSOLE_FONT
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
      return true;
    }
    return false;
  }
#endif
  return false;
}

int main(int argc, char* argv[])
{
  char* z_err_msg;
  HANDLE console_output = GetStdHandle(STD_OUTPUT_HANDLE);
  HANDLE logger_mutex = CreateMutex(NULL, FALSE, NULL);

  setlocale(LC_ALL, "ru_RU.utf8");
  SetConsoleCP(65001);

  bool font_ok = set_console_font(L"Lucida Console");

  Logger log_base("CardBluffServer.log", console_output, logger_mutex);
  Logger logger(L"Main");
  Logger sqlite_logger(L"SQLite");

  if(!font_ok)
    logger(L"Changing console font failed, you need to set it manually!");

  logger(L"CardBluff Server started!");

  if(!sqlite3_threadsafe()){
    sqlite_logger(L"Current SQLite version is not thread safe.");
    return 0;
  };

  sqlite3* db;
  sqlite_logger << L"Opening database '" << db_filename << L"'..." << Logger::endline;
  if(sqlite3_open(db_filename, &db)){
    sqlite_logger << L"Error opening database '" << db_filename << L"'" << Logger::endline;
    sqlite3_close(db);
    return 0;
  };
  sqlite_logger << L"Database file '" << db_filename << L"' opened" << Logger::endline;

  sqlite_logger(L"Checking client table existence and creating it if not exists...");
  if(sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS clients(id INTEGER PRIMARY KEY,nickname TEXT UNIQUE,password TEXT,rating INTEGER);", NULL, nullptr, &z_err_msg)){
    sqlite_logger << L"Error: " << z_err_msg << Logger::endline;
    sqlite3_free(z_err_msg);
    sqlite3_close(db);
    return 0;
  }
  sqlite_logger(L"Client table exists");

  SOCKET main_socket;
  DWORD thread;

  WSADATA wsaData;

  logger(L"WSA Startup...");
  int ret = WSAStartup(0x101, &wsaData); // use highest version of winsock avalible

  if(ret != 0){
    logger << "WSA Startup error: " << ret << Logger::endline;
    return 0;
  }
  logger(L"WSA Startup OK");

  sockaddr_in server;

  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(PORT);

  logger(L"Creating main socket...");
  main_socket = socket(AF_INET, SOCK_STREAM, 0);

  if(main_socket == INVALID_SOCKET){
    logger(L"Error creating socket!");
    return 0;
  }
  logger(L"Main socket created");

  logger << L"Binding socket to port " << PORT << L"..." << Logger::endline;
  if(bind(main_socket, (sockaddr*)&server, sizeof(server)) !=0){
    logger(L"Error binding socket!");
    return 0;
  }

  logger(L"Starting listening on socket...");
  if(listen(main_socket, MAX_CLIENTS) != 0){
    logger(L"Error starting listening on socket!");
    return 0;
  }
  logger(L"Listening started");

  SOCKET client_socket;

  sockaddr_in from;
  int fromlen = sizeof(from);

  logger(L"Creating duel finding thread...");
  CreateThread(NULL, 0, find_duel_thread, (LPVOID) 0, 0, &thread);

  logger(L"Waiting for clients...");
  long long client_num = 0;
  while(true){
    client_socket = accept(main_socket, (struct sockaddr*) &from, &fromlen);
    logger << "New client #" << client_num << " connected" << Logger::endline;

    logger << "Client #" << client_num << ": Putting socket into non-blocking mode..." << Logger::endline;
    unsigned long non_blocking = 1;
    if (ioctlsocket(client_socket, FIONBIO, &non_blocking) == SOCKET_ERROR) {
      logger << "Client #" << client_num << ": Error: failed to put socket into non-blocking mode" << Logger::endline;
      logger << "Client #" << client_num << ": Closing socket..." << Logger::endline;
      closesocket(client_socket);
      continue;
    }
    logger << "Client #" << client_num << ": Socket is non-blocking now" << Logger::endline;

    Client* client = new Client(client_socket);
    clients.insert(client);

    logger << "Client #" << client_num << ": Staring threads..." << Logger::endline;
    client->set_send_thread(CreateThread(NULL, 0, server_to_client, (LPVOID) client, 0, &thread));
    client->set_receive_thread(CreateThread(NULL, 0, client_to_server, (LPVOID) client, 0, &thread));
    logger << "Client #" << client_num << ": Threads started" << Logger::endline;

    client_num++;
  }

  closesocket(main_socket);
  WSACleanup();

  CloseHandle(logger_mutex);
  return 0;
}
