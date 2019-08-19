#include "game.h"
#include "common.h"

#include <cstdio>
#include <cwchar>
#include <cwctype>

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
    thread_handle_ready_event = CreateEvent(NULL, TRUE, FALSE, NULL);

    InitializeCriticalSection(&finish_critical_section);
    InitializeCriticalSection(&make_move_critical_section);

    first_player_card_number = START_CARD_NUMBER;
    second_player_card_number = START_CARD_NUMBER;

    first_move = (gnr() & 1) ? FIRST_PLAYER_MOVE : SECOND_PLAYER_MOVE;
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
      push_string(nullptr, SERVER_PREFIX L" You haven't done your move in time.");

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

   // finish(get_currently_moving_player(), _terminate);
    //*_terminate = true;
    //return;


    break;

  case WAIT_OBJECT_0 + 1: //terminate
    *_terminate = true;
    return;
    break;

  default:

    break;
  }
}

void Game::push_string_to_both(bool* _terminate, const wstring &str){
  first_player->push_string(str, _terminate);
  second_player->push_string(str, _terminate);
}


void Game::start_round(bool* _terminate){
  //TODO: Start new round (generate cards and initialize other game state vars).
  current_combination.clear();
  current_combination.push_back(NOTHING);
  alternate_first_move();
  generate_cards();

  send_card_messages_to_owners(_terminate);
  if(_terminate != nullptr && *_terminate)
    return;
  send_next_move_prompts(_terminate);
  if(_terminate != nullptr && *_terminate)
    return;
}

vector<CARD_TYPE> Game::generate_shuffled_array_of_cards()
{
    vector<CARD_TYPE> arr(ALL_CARDS);
    for (CARD_TYPE i = 0; i < ALL_CARDS; ++i)
        arr[i] = i;
    for (CARD_TYPE i = ALL_CARDS - 1; i != CARD_TYPE_MAX; --i)
        swap(arr[i], arr[gnr() % (i + 1)]);
    return arr;
}

extern void log(const char* format, ...);

void Game::generate_cards()
{
    log("Generating cards: %u, %u\n", (unsigned int) first_player_card_number, (unsigned int) second_player_card_number);
    vector<CARD_TYPE> my_cards = generate_shuffled_array_of_cards();
    first_player_cards.clear();
    second_player_cards.clear();
    copy(my_cards.begin(), my_cards.begin() + first_player_card_number, back_inserter(first_player_cards));
    copy(my_cards.begin() + first_player_card_number, my_cards.begin() + first_player_card_number + second_player_card_number, back_inserter(second_player_cards));
    my_cards.resize(first_player_card_number + second_player_card_number);
    union_of_cards = Hand(my_cards);
}

bool Game::is_valid_command(const std::wstring& command){
    // TODO: do it
  if(command == L"/concede")
    return true;
  return true;
}

std::wstring remove_spaces(const std::wstring& str)
{
    std::wstring ans;
    for (auto it = str.begin(); it != str.end(); ++it)
        if (*it != L' ')
            ans.push_back(*it);
    return ans;
}
void Game::player_loses_round(bool* _terminate, const CurrentMove& cur)
{
    ++card_number[cur];
    RoundResult res;
    switch (game_result())
    {
        case FIRST_PLAYER_MOVE:
        {
            res = FIRST_PLAYER_LOST_GAME;
            break;
        }
        case SECOND_PLAYER_MOVE:
        {
            res = SECOND_PLAYER_LOST_GAME;
            break;
        }
        case 2:
        {
            res = TIE_IN_GAME;
            break;
        }
        case 3:
        {
            res = (cur == FIRST_PLAYER_MOVE ? FIRST_PLAYER_LOST_ROUND : SECOND_PLAYER_LOST_ROUND);
            break;
        }
    }
    report_round_results(_terminate, res);
    if (FinishedGame(res))
        finish(res, _terminate);
}
void Game::tie_in_round(bool* _terminate)
{
    report_round_results(_terminate, TIE_IN_ROUND);
}
void Game::report_round_results(bool* _terminate, const RoundResult& res)
{
    send_card_messages_to_both_players(_terminate);
    send_card_numbers_to_both_players(_terminate, res);
}
// lost player, 2 for a tie, 3 for a continuation
uint8_t Game::game_result() const
{
    assert(first_player_card_number <= TIE_CARD_NUMBER);
    assert(second_player_card_number <= TIE_CARD_NUMBER);
    assert(first_player_card_number >= START_CARD_NUMBER);
    assert(second_player_card_number >= START_CARD_NUMBER);
    if ((first_player_card_number == TIE_CARD_NUMBER) && (second_player_card_number == TIE_CARD_NUMBER))
        return 2;
    if (((first_player_card_number < EQUALITY_CARD_NUMBER) && (second_player_card_number < EQUALITY_CARD_NUMBER)) ||
        (first_player_card_number == second_player_card_number) ||
        (first_player_card_number + 1 == second_player_card_number) ||
        (first_player_card_number == second_player_card_number + 1))
        return 3;
    if (first_player_card_number > second_player_card_number)
        return FIRST_PLAYER_MOVE;
    else
        return SECOND_PLAYER_MOVE;
}
// TODO: use everywhere the two following functions
void Game::push_client_string_to_both(bool* _terminate, const wstring &str, Client* cl = nullptr)
{
    wstring addend = cl ? USER_PREFIX + cl->get_nickname() + L":" : SERVER_PREFIX;
    push_string_to_both(_terminate, addend + L": " + str);
}
void push_client_string_to_client(bool* _terminate, const wstring &str, Client* receiver, Client* sender = nullptr)
{
    wstring addend = sender ? USER_PREFIX + sender->get_nickname() + L":" : SERVER_PREFIX;
    receiver->push_string(_terminate, (addend + L" " + str).c_str());
}
void Game::make_move(Client* client, const std::wstring& command, bool* _terminate){
  *_terminate = false;

  while(!TryEnterCriticalSection(&make_move_critical_section)){
    if(WaitForSingleObject(terminate_event, 10) == WAIT_OBJECT_0){
      *_terminate = true;
      return;
    }
  }
  if(WaitForSingleObject(terminate_event, 0) == WAIT_OBJECT_0){
      *_terminate = true;
      return;
  }

  if(!is_valid_command(command)){
    client->push_string(_terminate, SERVER_PREFIX L" Wrong command!");
    LeaveCriticalSection(&make_move_critical_section);
    return;
  }

  if(command == L"/concede"){
      report_round_results(_terminate, client == first_player ? FIRST_PLAYER_LOST_GAME : SECOND_PLAYER_LOST_GAME);
      finish(client == first_player ? FIRST_PLAYER_LOST_GAME : SECOND_PLAYER_LOST_GAME, _terminate);
      LeaveCriticalSection(&make_move_critical_section);
      return;
  }

  std::wstring cws = remove_spaces(command); // command_without_spaces
  std::wstring lcws;                         // lowered_command_without_spaces
  //std::transform(cws.begin(), cws.end(), lcws.begin(), ::towlower);
  for(auto it: cws)
    lcws.push_back(towlower(it));

  if(makes_current_move(client)) {
    if (lcws == L"/r")
    {
        if (Hand::is_combination_nothing(current_combination))
            push_client_string_to_client(_terminate, client->get_nickname() + L", запрещено вскрываться на первом ходу.", client); // TODO: ENGLISH
        else if (union_of_cards.check_combination(current_combination))
        {
            push_client_string_to_client(_terminate, cws, client, get_currently_not_moving_player());
            push_client_string_to_both(_terminate, client->get_nickname() + L", здесь есть эта комбинация."); // TODO: ENGLISH
            player_loses_round(_terminate, current_move);
        }
        else
        {
            push_client_string_to_client(_terminate, cws, client, get_currently_not_moving_player());
            push_client_string_to_both(_terminate, client->get_nickname() + L", здесь нет этой комбинации."); // TODO: ENGLISH
            player_loses_round(_terminate, negation(current_move));
        }
    }
    else if (lcws == L"/b")
    {
        push_client_string_to_client(_terminate, cws, client, get_currently_not_moving_player());
        if (Hand::is_combination_nothing(current_combination))
            push_client_string_to_client(_terminate, client->get_nickname() + L", запрещено блокировать на первом ходу.", client); // TODO: ENGLISH
        else if (union_of_cards.is_best_combination(current_combination))
        {
            push_client_string_to_client(_terminate, cws, client, get_currently_not_moving_player());
            push_string_to_both(_terminate, SERVER_PREFIX L" " + client->get_nickname() + L", это лучшая комбинация."); // TODO: ENGLISH
            tie_in_round(_terminate);
        }
        else if (union_of_cards.check_combination(current_combination))
        {
            push_client_string_to_client(_terminate, cws, client, get_currently_not_moving_player());
            push_string_to_both(_terminate, SERVER_PREFIX L" " + client->get_nickname() + L", этой комбинации здесь нет."); // TODO: ENGLISH
            player_loses_round(_terminate, current_move);
        }
        else
        {
            push_client_string_to_client(_terminate, cws, client, get_currently_not_moving_player());
            push_string_to_both(_terminate, SERVER_PREFIX L" " + client->get_nickname() + L", это не лучшая комбинация."); // TODO: ENGLISH
                                                                                                                    // TODO: Write the best combination
            player_loses_round(_terminate, current_move);
        }
    }
    else if (wcsncmp(lcws.c_str(), L"/m", 2) == 0)
    {

    }
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
      push_string(_terminate, SERVER_PREFIX L" It's not your move now!");
    if(*_terminate){
      LeaveCriticalSection(&make_move_critical_section);
      return;
    }
    get_currently_moving_player()->
      push_string(_terminate, SERVER_PREFIX L" Your opponent "
                  L"tried to make a move: '%s'.", command.c_str());
    if(*_terminate){
      LeaveCriticalSection(&make_move_critical_section);
      return;
    }
  }
  LeaveCriticalSection(&make_move_critical_section);
}/*
void Game::finish_round(Client* client, bool* _terminate)
{
  log("Finishing the round...\n");
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
}*/
void Game::finish(const RoundResult& res, bool* _terminate){
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
/*
  Client* loser = client;
  Client* winner = (client == first_player) ? second_player : first_player;

  wchar_t str[BUFFER_SIZE];

  log("Sending number of cards...\n");
  swprintf(str, L"SERVER: Number of cards:\nSERVER: %s: %s\nSERVER: %s: %s",
          first_player->get_nickname().c_str(), (first_player == loser) ? L"Lost" : ll_to_wstring(first_player_card_number).c_str(),
          second_player->get_nickname().c_str(), (second_player == loser) ? L"Lost" : ll_to_wstring(second_player_card_number).c_str());
  push_string_to_both(_terminate, wstring(str));
  log("Numbers of cards sent.\n");
*/
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
      push_string(_terminate, SERVER_PREFIX L" %s, you have %u seconds to move.",
                  get_currently_moving_player()->get_nickname().c_str(),
                  (unsigned int) (MOVE_TIMEOUT / 1000));
    if(_terminate != nullptr && *_terminate){
      return;
    }
    get_currently_not_moving_player()->
      push_string(_terminate, SERVER_PREFIX L" Waiting your opponent to move...");
    if(_terminate != nullptr && *_terminate){
      return;
    }
}

wstring cards_to_string(vector<CARD_TYPE> &cards){
  wchar_t str[BUFFER_SIZE];
  wchar_t* ptr = str;
  swprintf(ptr, CARDS_PREFIX L"%01u", (unsigned) cards.size());
  ptr += 7;
  for(int i = 0; i < cards.size(); i++){
    CARD_TYPE value = cards[i] / 4;
    CARD_TYPE suit = cards[i] % 4;

    wchar_t value_c;

    switch(value){
    case TWO: value_c = L'2'; break;
    case THREE: value_c = L'3'; break;
    case FOUR: value_c = L'4'; break;
    case FIVE: value_c = L'5'; break;
    case SIX: value_c = L'6'; break;
    case SEVEN: value_c = L'7'; break;
    case EIGHT: value_c = L'8'; break;
    case NINE: value_c = L'9'; break;
    case TEN: value_c = L'0'; break;
    case JACK: value_c = L'J'; break;
    case QUEEN: value_c = L'Q'; break;
    case KING: value_c = L'K'; break;
    case ACE: value_c = L'A'; break;
    }

    swprintf(ptr, L":%01u,%c", (unsigned) suit, value_c);
    ptr += 4;
  }

  return wstring(str);
}
void Game::send_card_messages_to_one_player(bool* _terminate, Client* client){
  push_client_string_to_client(_terminate, L"Cards", client);
    if(_terminate != nullptr && *_terminate){
      return;
    }
  client->push_string(cards_to_string(first_player_cards) + first_player->get_nickname() + L": ", _terminate);
    if(_terminate != nullptr && *_terminate){
      return;
    }
  client->push_string(cards_to_string(second_player_cards) + second_player->get_nickname() + L": ", _terminate);
    if(_terminate != nullptr && *_terminate){
      return;
    }
}
void Game::send_card_messages_to_both_players(bool* _terminate){
  log("Sending card messages to both players\n");
  if(_terminate != nullptr)
    *_terminate = false;
  send_card_messages_to_one_player(_terminate, first_player);
  bool finally_terminate = (_terminate != nullptr && *_terminate);

  if(_terminate != nullptr)
    *_terminate = false;

  send_card_messages_to_one_player(_terminate, second_player);

  if(_terminate != nullptr && (*_terminate == false)){
    *_terminate = finally_terminate;
  }
}
void Game::send_card_numbers_to_one_player(bool* _terminate, Client* client, const wstring& zero_line, const wstring& first_line, const wstring& second_line)
{
    push_client_string_to_client(_terminate, zero_line, client);
    if(_terminate != nullptr && *_terminate){
      return;
    }
    push_client_string_to_client(_terminate, first_line, client);
    if(_terminate != nullptr && *_terminate){
      return;
    }
    push_client_string_to_client(_terminate, second_line, client);
    if(_terminate != nullptr && *_terminate){
      return;
    }
}
void Game::send_card_numbers_to_both_players(bool* _terminate, const RoundResult& res)
{
    log("Sending card numbers to both players\n");
    std::wstring zero_line, first_line, second_line;
    zero_line = L"Number of cards:";

    first_line = first_player->get_nickname() + L": ";
    if (res == FIRST_PLAYER_LOST_GAME)
        first_line += L"Lost";
    else
    {
        first_line += ll_to_wstring(first_player_card_number);
        if (res == FIRST_PLAYER_LOST_ROUND)
            first_line += L" :|";
        else if (res == TIE_IN_GAME)
            first_line += L" :facepalm:";
    }

    second_line = second_player->get_nickname() + L": ";
    if (res == SECOND_PLAYER_LOST_GAME)
        second_line += L"Lost";
    else
    {
        second_line += ll_to_wstring(second_player_card_number);
        if (res == SECOND_PLAYER_LOST_ROUND)
            second_line += L" :|";
        else if (res == TIE_IN_GAME)
            second_line += L" :facepalm:";
    }

    send_card_numbers_to_one_player(_terminate, first_player, zero_line, first_line, second_line);

    bool finally_terminate = (_terminate != nullptr && *_terminate);

    if(_terminate != nullptr)
        *_terminate = false;

    send_card_numbers_to_one_player(_terminate, second_player, zero_line, first_line, second_line);

    if(_terminate != nullptr && (*_terminate == false)){
        *_terminate = finally_terminate;
    }
}

void Game::send_card_messages_to_owners(bool* _terminate){
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

  first_player->push_string(cards_to_string(first_player_cards), _terminate);
  bool finally_terminate = (_terminate != nullptr && *_terminate);

  if(_terminate != nullptr)
    *_terminate = false;

  second_player->push_string(cards_to_string(second_player_cards), _terminate);

  if(_terminate != nullptr && (*_terminate == false)){
    *_terminate = finally_terminate;
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
    swprintf(message_first, SERVER_PREFIX L" %s: %u cards :(",
            first_player->get_nickname().c_str(),
            6/*put new number of cards here*/);
    swprintf(message_first, SERVER_PREFIX L" %s: %u cards",
            second_player->get_nickname().c_str(),
            5/*put new number of cards here*/);
  }
  else if(client->get_id() == first_player->get_id()){
    //Second player lost round
    swprintf(message_first, SERVER_PREFIX L" %s: %u cards",
            first_player->get_nickname().c_str(),
            5/*put new number of cards here*/);
    swprintf(message_first, SERVER_PREFIX L" %s: %u cards :(",
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

CurrentMove negation(const CurrentMove& my_move)
{
  if(my_move == FIRST_PLAYER_MOVE)
    return SECOND_PLAYER_MOVE;
  else if(my_move == SECOND_PLAYER_MOVE)
    return FIRST_PLAYER_MOVE;

  assert(false);
  return FIRST_PLAYER_MOVE;
}

void Game::alternate_current_move() {
  current_move = negation(current_move);
}

void Game::alternate_first_move(){
  first_move = negation(first_move);
  current_move = first_move;
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

Client* Game::get_first_player() const
{
    return first_player;
}
Client* Game::get_second_player() const
{
    return second_player;
}

bool Game::makes_current_move(Client* client){
  return get_currently_moving_player()->get_id() == client->get_id();
}
