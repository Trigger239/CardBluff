#ifndef CLIENT_H
#define CLIENT_H

#include <windows.h>
#include <winsock.h>
#include <queue>
#include <string>
#include "common.h"
#include "game.h"
class Game;

enum ClientState{
  WAIT_NICKNAME,
  WAIT_PASSWORD_REGISTER_FIRST,
  WAIT_PASSWORD_REGISTER_SECOND,
  WAIT_PASSWORD,
  WAIT_ENTER_GAME,
  WAIT_OPPONENT,
  IN_GAME
};

class Client{
public:

  Client(const SOCKET& socket,
         const std::string& nickname,
         long long id);
  Client(const SOCKET& socket);
  ~Client();

  HANDLE _get_mutex();
  HANDLE get_terminate_event();
  HANDLE get_reconnect_event();

  void set_waiting_reconnect(bool _waiting_reconnect);
  bool get_waiting_reconnect();

  void set_send_thread(HANDLE thread);
  HANDLE get_send_thread();

  void set_receive_thread(HANDLE thread);
  HANDLE get_receive_thread();

  void set_socket(SOCKET _socket);
  SOCKET get_socket();

  void set_nickname(const std::string& _nickname);
  void set_nickname(const char* _nickname);
  std::string get_nickname();

  void set_id(long long _id);
  long long get_id();

  void set_state(ClientState _state);
  ClientState get_state();

  void set_game(Game* _game);
  Game* get_game();

  void set_opponent(Client* _opponent);
  Client* get_opponent();

  void set_finding_duel(bool _finding_duel);
  bool get_finding_duel();

  void set_in_game(bool _in_game);
  bool get_in_game();

  void set_authorized(bool _authhorized);
  bool get_authorized();

  Game* enter_game(Client* opponent);

  int receive_data(char* data, int data_size, bool* _terminate = nullptr);
  int send_data(const char* data, int data_size, bool* _terminate = nullptr);

  void push_string(std::string str, bool* _terminate = nullptr);
  void push_string(bool* _terminate, const char* format, ...);
  bool queue_is_empty(bool* _terminate = nullptr);
  int send_from_queue(bool* _terminate = nullptr);
  void copy_strings(Client* dest_client, bool* _terminate = nullptr);

  void close_socket();

private:

  HANDLE mutex;
  HANDLE send_thread;
  HANDLE receive_thread;
  HANDLE reconnect_event;
  HANDLE terminate_event;
  SOCKET socket;
  HANDLE send_queue_mutex;
  std::queue<std::string> send_queue;

  std::string nickname;
  long long id;
  ClientState state;
  Game* game;
  Client* opponent;
  bool waiting_reconnect;
  bool finding_duel;
  bool in_game;
  bool authorized;

  int wait_mutex();
  int wait_mutex(HANDLE _mutex);

  template<typename T>
  void set_var(T& var, T value);
  template<typename T>
  T get_var(T& var);
};

#endif // CLIENT_H
