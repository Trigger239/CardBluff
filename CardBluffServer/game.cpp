#include "game.h"
#include "common.h"

#include <cstdio>

#define BUFFER_SIZE 1000

Game::Game(Client* first_player,
           Client* second_player,
           CurrentMove current_move)
  : first_player(first_player)
  , second_player(second_player)
  , current_move(current_move)
  , gnr(chrono::high_resolution_clock::now().time_since_epoch().count())
  {
    terminate_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    move_event = CreateEvent(NULL, TRUE, FALSE, NULL);

    InitializeCriticalSection(&finish_critical_section);
    InitializeCriticalSection(&make_move_critical_section);

    first_player_card_number = START_CARD_NUMBER;
    second_player_card_number = START_CARD_NUMBER;

    //TODO: Initialize game state vars (like number of cards)
  };

Game::~Game(){
  CloseHandle(terminate_event);
  CloseHandle(move_event);
  CloseHandle(thread_handle_ready_event);
  DeleteCriticalSection(&finish_critical_section);
  DeleteCriticalSection(&make_move_critical_section);
}

void Game::process(bool* _terminate){
  *_terminate = false;
  HANDLE events[2] = {move_event, terminate_event};

  switch(WaitForMultipleObjects(2, events, FALSE, MOVE_TIMEOUT)){
  case WAIT_OBJECT_0 + 0: //move was successfully made
    ResetEvent(move_event);
    break;

  case WAIT_TIMEOUT: //move timeout ends
    get_currently_moving_player()->
      push_string(nullptr, L"SERVER: You haven't done your move in time.");

    //TODO: convert this pseudocode to normal code (see also make_move())
//    compute_new_number_of_cards(get_currently_moving_player(), _terminate);
//    if(!last_round){
//      send_round_result_messages(get_currently_moving_player());
//      start_round();
//    }
//    else{
//      finish(get_currently_moving_player(), _terminate);
//      //if(*_terminate)
//      //  return;
//
//      *_terminate = true;
//      return;
//    }
    //TODO: And delete next 3 lines.

    finish(get_currently_moving_player(), _terminate);
    *_terminate = true;
    return;


    break;

  case WAIT_OBJECT_0 + 1: //terminate
    *_terminate = true;
    return;
    break;

  default:

    break;
  }
}

void Game::start_round(bool* _terminate){
  //TODO: Start new round (generate cards and initialize other game state vars).

  generate_cards();

  send_card_messages(_terminate);
  if(_terminate != nullptr && *_terminate)
    return;
  send_next_move_prompts(_terminate);
  if(_terminate != nullptr && *_terminate)
    return;
}

vector<uint8_t> Game::generate_shuffled_array_of_cards()
{
    vector<uint8_t> arr(ALL_CARDS);
    for (uint8_t i = 0; i < ALL_CARDS; ++i)
        arr[i] = i;
    for (uint8_t i = ALL_CARDS - 1; i != UINT8_MAX; --i)
        swap(arr[i], arr[gnr() % (i + 1)]);
    return arr;
}

extern void log(const char* format, ...);

void Game::generate_cards()
{
    log("Generating cards: %u, %u\n", (unsigned int) first_player_card_number, (unsigned int) second_player_card_number);
    vector<uint8_t> my_cards = generate_shuffled_array_of_cards();
    first_player_cards.clear();
    second_player_cards.clear();
    copy(my_cards.begin(), my_cards.begin() + first_player_card_number, back_inserter(first_player_cards));
    copy(my_cards.begin() + first_player_card_number, my_cards.begin() + first_player_card_number + second_player_card_number, back_inserter(second_player_cards));
}

bool Game::is_valid_command(const std::wstring& command){
  if(command == L"/concede")
    return true;
  return true;
}

void Game::make_move(Client* client, const std::wstring& command, bool* _terminate){
  *_terminate = false;

  while(!TryEnterCriticalSection(&make_move_critical_section)){
    if(WaitForSingleObject(terminate_event, 10)){
      *_terminate = true;
      return;
    }
  }

  if(!is_valid_command(command)){
    client->push_string(_terminate, L"SERVER: Wrong command!");
    LeaveCriticalSection(&make_move_critical_section);
    return;
  }

  if(command == L"/concede"){
      finish(client, _terminate);
      LeaveCriticalSection(&make_move_critical_section);
      return;
  }

  if(makes_current_move(client)){

    //TODO: Parse and process move command
    //TODO: convert this pseudocode to normal code (see also process())
//    if(round_ends){
//      compute_new_number_of_cards(get_currently_moving_player(), _terminate);
//      if(!last_round){
//        send_round_result_messages(get_currently_moving_player());
//        start_round();
//      }
//      else{
//        finish(get_currently_moving_player());
//        LeaveCriticalSection(&make_move_critical_section);
//        return;
//      }
//    }

    //Sending move command to currently not moving player
    get_currently_not_moving_player()->
      push_string(_terminate, L"%s: %s",
                  get_currently_moving_player()->get_nickname().c_str(),
                  command.c_str());
    if(*_terminate){
      LeaveCriticalSection(&make_move_critical_section);
      return;
    }



    //Prepare the next move
    alternate_current_move();
    send_next_move_prompts(_terminate);
    if(*_terminate){
      LeaveCriticalSection(&make_move_critical_section);
      return;
    }

    SetEvent(move_event);
  }
  else{
    get_currently_not_moving_player()->
      push_string(_terminate, L"SERVER: It's not your move now!");
    if(*_terminate){
      LeaveCriticalSection(&make_move_critical_section);
      return;
    }
    get_currently_moving_player()->
      push_string(_terminate, L"SERVER: Your opponent "
                  L"tried to make a move: '%s'.", command.c_str());
    if(*_terminate){
      LeaveCriticalSection(&make_move_critical_section);
      return;
    }
  }
  LeaveCriticalSection(&make_move_critical_section);
}

void Game::finish(Client* client, bool* _terminate){
  log("Finishing the game...\n");
  if(_terminate != nullptr)
    *_terminate = false;
  while(!TryEnterCriticalSection(&finish_critical_section)){
    if(WaitForSingleObject(terminate_event, 10)){
      if(_terminate != nullptr)
        *_terminate = true;
      return;
    }
  }

  //This code protects from finishing from two different thread
  //(is both clients are disconnected or one of them loses etc.).
  if(WaitForSingleObject(terminate_event, 0) == WAIT_OBJECT_0){
    LeaveCriticalSection(&finish_critical_section);
    return;
  }
  //TODO: Process the LOSE of the client (send messages, update ratings).
  Client* loser = client;
  Client* winner = (client == first_player) ? second_player : first_player;

  wchar_t str[BUFFER_SIZE];

  log("Sending number of cards...\n");
  swprintf(str, L"SERVER: Number of cards:\nSERVER: %s: %s\nSERVER: %s: %s",
          first_player->get_nickname().c_str(), (first_player == loser) ? L"Lost" : ll_to_wstring(first_player_card_number).c_str(),
          second_player->get_nickname().c_str(), (second_player == loser) ? L"Lost" : ll_to_wstring(second_player_card_number).c_str());
  push_string_to_both(_terminate, wstring(str));
  log("Numbers of cards sent.\n");

  first_player->set_in_game(false);
  second_player->set_in_game(false);
  first_player->set_state(WAIT_ENTER_GAME);
  second_player->set_state(WAIT_ENTER_GAME);

  SetEvent(terminate_event);

  LeaveCriticalSection(&finish_critical_section);
};

void Game::send_next_move_prompts(bool* _terminate){
    if(_terminate != nullptr)
      *_terminate = false;
    get_currently_moving_player()->
      push_string(_terminate, L"SERVER: %s, you have %u seconds to move.",
                  get_currently_moving_player()->get_nickname().c_str(),
                  (unsigned int) (MOVE_TIMEOUT / 1000));
    if(_terminate != nullptr && *_terminate){
      return;
    }
    get_currently_not_moving_player()->
      push_string(_terminate, L"SERVER: Waiting your opponent to move...");
    if(_terminate != nullptr && *_terminate){
      return;
    }
}

wstring cards_to_string(vector<uint8_t> &cards){
  wchar_t str[BUFFER_SIZE];
  wchar_t* ptr = str;
  swprintf(ptr, L"cards:%01u", (int) cards.size());
  ptr += 7;
  for(int i = 0; i < cards.size(); i++){
    uint8_t value = cards[i] / 4;
    uint8_t suit = cards[i] % 4;

    wchar_t value_c;
    if(value < 10)
      value_c = value + L'0';
    else if(value == 10)
      value_c = L'J';
    else if(value == 11)
      value_c = L'Q';
    else if(value == 12)
      value_c = L'K';
    else
      value_c = L'A';

    swprintf(ptr, L":%01u,%c", (int) suit, value_c);
    ptr += 4;
  }

  return wstring(str);
}

void Game::send_card_messages(bool* _terminate){
  log("Sending card messages\n");
  if(_terminate != nullptr)
    *_terminate = false;
  //TODO: Send real cards according to current game state.
  //      Card message format:
  //      cards:<number_of_cards>:<suit>,<value>[:<suit>,<value>]
  //      where cards - message keyword,
  //            <number_of_cards> - integer, number of suit-value pairs
  //                                in the message,
  //            <suit> - integer from 0 to 3,
  //            <value> - single char '2' - '9', '0', 'J', 'Q', 'K' or 'A'

  first_player->
    push_string(_terminate, cards_to_string(first_player_cards).c_str());
  if(_terminate != nullptr && *_terminate){
    return;
  }

  second_player->
    push_string(_terminate, cards_to_string(second_player_cards).c_str());
  if(_terminate != nullptr && *_terminate){
    return;
  }
}

//client is a loser of a round
void Game::send_round_result_messages(Client* client, bool* _terminate){
  if(_terminate != nullptr)
    *_terminate = false;

  wchar_t message_first[BUFFER_SIZE], message_second[BUFFER_SIZE];

  //TODO: Send real number of cards

  if(client->get_id() == first_player->get_id()){
    //First player lost round
    swprintf(message_first, L"SERVER: %s: %u cards :(",
            first_player->get_nickname().c_str(),
            6/*put new number of cards here*/);
    swprintf(message_first, L"SERVER: %s: %u cards",
            second_player->get_nickname().c_str(),
            5/*put new number of cards here*/);
  }
  else if(client->get_id() == first_player->get_id()){
    //Second player lost round
    swprintf(message_first, L"SERVER: %s: %u cards",
            first_player->get_nickname().c_str(),
            5/*put new number of cards here*/);
    swprintf(message_first, L"SERVER: %s: %u cards :(",
            second_player->get_nickname().c_str(),
            6/*put new number of cards here*/);
  }
  else
    return;

  first_player->
    push_string(_terminate, message_first);
  if(_terminate != nullptr && *_terminate){
    return;
  }
  first_player->
    push_string(_terminate, message_second);
  if(_terminate != nullptr && *_terminate){
    return;
  }
  second_player->
    push_string(_terminate, message_first);
  if(_terminate != nullptr && *_terminate){
    return;
  }
  second_player->
    push_string(_terminate, message_second);
  if(_terminate != nullptr && *_terminate){
    return;
  }
}

HANDLE Game::get_thread_handle_ready_event(){
  return thread_handle_ready_event;
}

void Game::set_thread(HANDLE _thread){
  thread = _thread;
}

HANDLE Game::get_thread(){
  return thread;
}

void  Game::alternate_current_move(){
  if(current_move == FIRST_PLAYER_MOVE)
    current_move = SECOND_PLAYER_MOVE;
  else if(current_move == SECOND_PLAYER_MOVE)
    current_move = FIRST_PLAYER_MOVE;
}

Client* Game::get_currently_moving_player(){
  if(current_move == FIRST_PLAYER_MOVE)
    return first_player;
  if(current_move == SECOND_PLAYER_MOVE)
    return second_player;
  return nullptr;
}

Client* Game::get_currently_not_moving_player(){
  if(current_move == FIRST_PLAYER_MOVE)
    return second_player;
  if(current_move == SECOND_PLAYER_MOVE)
    return first_player;
  return nullptr;
}

bool Game::makes_current_move(Client* client){
  return get_currently_moving_player()->get_id() == client->get_id();
}

void Game::push_string_to_both(bool* _terminate, const wstring &str){
  first_player->push_string(str, _terminate);
  second_player->push_string(str, _terminate);
}
