#ifndef GAME_H
#define GAME_H

#include <windows.h>
#include <string>
#include <vector>
#include <inttypes.h>
#include <random>
#include <chrono>
#include <iterator>
#include <algorithm>
#include <cwctype>
#include <queue>

#include "client.h"
#include "combinations.h"
#include "database.h"
#include "util/logger.h"

using namespace std;

class Client;

enum CurrentMove{
  FIRST_PLAYER_MOVE = 0,
  SECOND_PLAYER_MOVE = 1
};
enum RoundResult{
  FIRST_PLAYER_LOST_ROUND = 0,
  SECOND_PLAYER_LOST_ROUND = 1,
  FIRST_PLAYER_LOST_GAME = 2,
  SECOND_PLAYER_LOST_GAME = 3,
  TIE_IN_ROUND = 4,
  TIE_IN_GAME = 5
};

typedef enum{
  MOVE_COMMAND,
  DISCONNECT,
} command_type_t;

struct Command{
  Client* sender;
  wstring cmd;
  CurrentMove move;
  command_type_t type;
  chrono::high_resolution_clock::time_point t;
  Command(Client* sender,
            wstring cmd,
            CurrentMove move,
            command_type_t type);
};

#define FinishedGame(x) ((x == FIRST_PLAYER_LOST_GAME) || (x == SECOND_PLAYER_LOST_GAME) || (x == TIE_IN_GAME))
CurrentMove negate_CurrentMove(const CurrentMove&);

class Game{
public:

  Game(Client* first_player,
       Client* second_player);
  ~Game();

  void push_command(Client* client, const wstring& command);
  void push_disconnect(Client* client);

  //This should be called from special game thread
  void process(bool* _terminate);

  //This should be called after the Game object created
  void start_round();

  //This should be called from client thread
  void make_move(Command cmd);

  //This should be called then client finishes the game by disconnect
  //or something else (this client always loses the game).
  void finish(const RoundResult& res);

  HANDLE get_thread_handle_ready_event();
  Logger& get_logger();

  void set_thread(HANDLE _thread);
  void set_db(sqlite3* _db);

  HANDLE get_thread();
  Client* get_first_player() const;
  Client* get_second_player() const;

private:
  sqlite3* db;

  HANDLE thread_handle_ready_event;
  HANDLE thread;
  HANDLE command_queue_mutex;

  queue<Command> command_queue;
  chrono::high_resolution_clock::time_point move_start_time;
  bool finished;

  union{
    struct{
      Client* first_player;
      Client* second_player;
    };
    Client* players[2];
  };
  union{
    struct{
      uint8_t first_player_card_number;
      uint8_t second_player_card_number;
    };
    uint8_t card_number[2];
  };
  Hand union_of_cards;
  vector<uint8_t> first_player_cards;
  vector<uint8_t> second_player_cards;
  CurrentMove current_move;
  CurrentMove first_move;
  mt19937 gnr;
  vector<int> current_combination;
  Logger logger;

  vector<uint8_t> generate_shuffled_array_of_cards();
  void generate_cards();
  wstring cards_to_string(vector<CARD_TYPE> &cards);

  void send_next_move_prompts();
  void send_card_messages_to_owners();
  void send_card_messages_to_both_players();
  //void send_round_result_messages(Client* client);

  void alternate_current_move();
  void alternate_first_move();
  Client* get_currently_moving_player();
  Client* get_currently_not_moving_player();
  bool makes_current_move(Client* client);
  void player_loses_round(const CurrentMove& cur);
  void tie_in_round();
  uint8_t game_result() const;
  void report_round_results(const RoundResult& res);
  void send_card_numbers_to_both_players(const RoundResult& res);
  void push_client_string_to_both(const wstring &str, Client* cl);
  void push_client_string_to_client(const wstring &str, Client* receiver, Client* sender = nullptr);
  void send_card_messages_to_one_player(Client* client);
  void push_string_to_both(const wstring &str);
  void send_card_numbers_to_one_player(Client* client, const wstring& zero_line, const wstring& first_line, const wstring& second_line);
  double get_remaining_move_time(chrono::high_resolution_clock::time_point now = chrono::high_resolution_clock::now());
};

#endif // GAME_H
