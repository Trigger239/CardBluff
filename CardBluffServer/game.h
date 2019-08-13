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

#include "client.h"

using namespace std;

class Client;

enum CurrentMove{
  FIRST_PLAYER_MOVE,
  SECOND_PLAYER_MOVE
};

class Game{
public:

  Game(Client* first_player,
       Client* second_player,
       CurrentMove current_move);
  ~Game();

  //This should be called from special game thread
  void process(bool* _terminate);

  //This should be called after the Game object created
  void start_round(bool* _terminate = nullptr);

  //This should be called from client thread
  void make_move(Client* client, const std::wstring& command, bool* _terminate);

  //This should be called then client finishes the game by disconnect
  //or something else (this client always loses the game).
  void finish(Client* client, bool* _terminate = nullptr);

  HANDLE get_thread_handle_ready_event();

  void set_thread(HANDLE _thread);
  HANDLE get_thread();

private:
  HANDLE terminate_event;
  HANDLE move_event;
  HANDLE thread_handle_ready_event;
  HANDLE thread;

  CRITICAL_SECTION finish_critical_section;
  CRITICAL_SECTION make_move_critical_section;

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
  vector<uint8_t> first_player_cards;
  vector<uint8_t> second_player_cards;
  CurrentMove current_move;
  CurrentMove first_move;
  mt19937 gnr;

  vector<uint8_t> generate_shuffled_array_of_cards();
  void generate_cards();

  bool is_valid_command(const std::wstring& command);

  void send_next_move_prompts(bool* _terminate = nullptr);
  void send_card_messages(bool* _terminate);
  void send_round_result_messages(Client* client, bool* _terminate);

  void alternate_current_move();
  Client* get_currently_moving_player();
  Client* get_currently_not_moving_player();
  bool makes_current_move(Client* client);

  void push_string_to_both(bool* _terminate, const wstring &str);
};

#endif // GAME_H
