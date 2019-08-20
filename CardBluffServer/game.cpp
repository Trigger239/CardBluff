#include "game.h"
#include "common.h"

#include <cstdio>
#include <cwchar>
#include <cwctype>

#define BUFFER_SIZE 1000


Command::Command(Client* sender,
          wstring cmd,
          CurrentMove move,
          command_type_t type = MOVE_COMMAND)
  : sender(sender)
  , cmd(cmd)
  , move(move)
  , type(type)
  , t(chrono::high_resolution_clock::now()){};

Game::Game(Client* first_player,
           Client* second_player,
           CurrentMove current_move)
  : finished(false)
  , first_player(first_player)
  , second_player(second_player)
  , current_move(current_move)
  , gnr(chrono::high_resolution_clock::now().time_since_epoch().count())
  {
    thread_handle_ready_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    command_queue_mutex = CreateMutex(NULL, FALSE, NULL);

    first_player_card_number = START_CARD_NUMBER;
    second_player_card_number = START_CARD_NUMBER;

    first_move = (gnr() & 1) ? FIRST_PLAYER_MOVE : SECOND_PLAYER_MOVE;
    //TODO: Initialize game state vars (like number of cards)
  };

Game::~Game(){
  CloseHandle(thread_handle_ready_event);
  CloseHandle(command_queue_mutex);
}

void Game::push_command(Client* client, const wstring& command){
  WaitForSingleObject(command_queue_mutex, INFINITE);
  command_queue.emplace(client, command, current_move);
  ReleaseMutex(command_queue_mutex);
}

void Game::push_disconnect(Client* client){
  WaitForSingleObject(command_queue_mutex, INFINITE);
  command_queue.emplace(client, L"", current_move, DISCONNECT);
  ReleaseMutex(command_queue_mutex);
}

void Game::process(bool* _terminate){
  *_terminate = false;

  WaitForSingleObject(command_queue_mutex, INFINITE);
  chrono::high_resolution_clock::time_point now = chrono::high_resolution_clock::now();

  if(!command_queue.empty()){
    Command cmd = command_queue.front();
    command_queue.pop();
    ReleaseMutex(command_queue_mutex);

    switch(cmd.type){
      case MOVE_COMMAND:
        make_move(cmd);
        break;

      case DISCONNECT:
        report_round_results(cmd.sender == first_player ? FIRST_PLAYER_LOST_GAME : SECOND_PLAYER_LOST_GAME);
        finish(cmd.sender == first_player ? FIRST_PLAYER_LOST_GAME : SECOND_PLAYER_LOST_GAME);
        *_terminate = true;
        break;
    }
  }
  else{
    ReleaseMutex(command_queue_mutex);
    Sleep(10);
  }

  if(!finished){
    if(chrono::duration_cast<chrono::duration<double>>(now - move_start_time).count() * 1000.0 > MOVE_TIMEOUT){

      push_client_string_to_client(COLOR_ESCAPE + get_currently_moving_player()->get_nickname() + COLOR_ESCAPE L", you haven't done your move in time!", get_currently_moving_player());
      push_client_string_to_client(COLOR_ESCAPE + get_currently_moving_player()->get_nickname() + COLOR_ESCAPE L" haven't done his move in time!", get_currently_not_moving_player());

      if(first_player_card_number == START_CARD_NUMBER &&
         second_player_card_number == START_CARD_NUMBER){
        report_round_results(TIE_IN_GAME);
        finish(TIE_IN_GAME);
      }
      else{
        player_loses_round(current_move);
      }
    }
  }
  else{
    *_terminate = true;
  }
}

void Game::push_string_to_both(const wstring &str){
  first_player->push_string(str);
  second_player->push_string(str);
}

void Game::start_round(){
  //TODO: Start new round (generate cards and initialize other game state vars).
  current_combination.clear();
  current_combination.push_back(NOTHING);
  alternate_first_move();
  generate_cards();

  move_start_time = chrono::high_resolution_clock::now();

  send_card_messages_to_owners();
  send_next_move_prompts();
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


void Game::player_loses_round(const CurrentMove& cur)
{
    ++card_number[cur];
    alternate_first_move();
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
    report_round_results(res);
    if (FinishedGame(res))
        finish(res);
}
void Game::tie_in_round()
{
    report_round_results(TIE_IN_ROUND);
}
void Game::report_round_results(const RoundResult& res)
{
    send_card_messages_to_both_players();
    send_card_numbers_to_both_players(res);
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
void Game::push_client_string_to_both(const wstring &str, Client* cl = nullptr)
{
    wstring addend = cl ? USER_PREFIX COLOR_ESCAPE + cl->get_nickname() + COLOR_ESCAPE L":" : SERVER_PREFIX;
    push_string_to_both(addend + L' ' + str);
}
void Game::push_client_string_to_client(const wstring &str, Client* receiver, Client* sender)
{
    wstring addend = sender ? USER_PREFIX COLOR_ESCAPE + sender->get_nickname() + COLOR_ESCAPE L":" : SERVER_PREFIX;
    receiver->push_string((addend + L' ' + str).c_str());
}
void Game::make_move(Command cmd){
  assert(cmd.type == MOVE_COMMAND);

  move_start_time = cmd.t;

  wstring command = cmd.cmd;
  Client* client = cmd.sender;
  std::wstring cws = remove_spaces(cmd.cmd); // command_without_spaces
  std::wstring lcws;                         // lowered_command_without_spaces
  std::transform(cws.begin(), cws.end(), back_inserter(lcws), ::towlower);

  if(lcws == L"/concede"){
      push_client_string_to_client(lcws, client == first_player ? second_player : first_player, client);

      push_client_string_to_client(SERVER_PREFIX L"You conceded!", client);
      push_client_string_to_client(SERVER_PREFIX COLOR_ESCAPE + client->get_nickname() + COLOR_ESCAPE L" conceded!",
                                   client == first_player ? second_player : first_player);

      report_round_results(client == first_player ? FIRST_PLAYER_LOST_GAME : SECOND_PLAYER_LOST_GAME);
      finish(client == first_player ? FIRST_PLAYER_LOST_GAME : SECOND_PLAYER_LOST_GAME);
      //LeaveCriticalSection(&make_move_critical_section);
      return;
  }

  if(makes_current_move(client)){
    //Sending move command to currently not moving player
    push_client_string_to_client(lcws, get_currently_not_moving_player(), client);

    if (lcws == L"/r")
    {
        if (Hand::is_combination_nothing(current_combination))
            push_client_string_to_client(COLOR_ESCAPE + client->get_nickname() + COLOR_ESCAPE L", запрещено вскрываться на первом ходу.", client); // TODO: ENGLISH
        else if (union_of_cards.check_combination(current_combination))
        {
            //push_client_string_to_client(cws, client, get_currently_not_moving_player());
            push_client_string_to_both(COLOR_ESCAPE + client->get_nickname() + COLOR_ESCAPE L", здесь есть эта комбинация."); // TODO: ENGLISH
            player_loses_round(current_move);
        }
        else
        {
            //push_client_string_to_client(cws, client, get_currently_not_moving_player());
            push_client_string_to_both(COLOR_ESCAPE + client->get_nickname() + COLOR_ESCAPE L", здесь нет этой комбинации."); // TODO: ENGLISH
            player_loses_round(negation(current_move));
        }
    }
    else if (lcws == L"/b")
    {
        //push_client_string_to_client(cws, client, get_currently_not_moving_player());
        if (Hand::is_combination_nothing(current_combination))
            push_client_string_to_client(COLOR_ESCAPE + client->get_nickname() + COLOR_ESCAPE L", запрещено блокировать на первом ходу.", client); // TODO: ENGLISH
        else if (union_of_cards.is_best_combination(current_combination))
        {
            //push_client_string_to_client(cws, client, get_currently_not_moving_player());
            push_string_to_both(SERVER_PREFIX L" " COLOR_ESCAPE + client->get_nickname() + COLOR_ESCAPE L", это лучшая комбинация."); // TODO: ENGLISH
            tie_in_round();
        }
        else if (union_of_cards.check_combination(current_combination))
        {
            //push_client_string_to_client(cws, client, get_currently_not_moving_player());
            push_string_to_both(SERVER_PREFIX L" " COLOR_ESCAPE + client->get_nickname() + COLOR_ESCAPE L", этой комбинации здесь нет."); // TODO: ENGLISH
            player_loses_round(current_move);
        }
        else
        {
            //push_client_string_to_client(cws, client, get_currently_not_moving_player());
            push_string_to_both(SERVER_PREFIX L" " COLOR_ESCAPE + client->get_nickname() + COLOR_ESCAPE L", это не лучшая комбинация."); // TODO: ENGLISH
                                                                                                                    // TODO: Write the best combination
            player_loses_round(current_move);
        }
    }
    else if (wcsncmp(lcws.c_str(), L"/m", 2) == 0)
    {
        vector<int> combination;
        //push_client_string_to_client(cws, client, get_currently_not_moving_player());
        wstring transcript = Hand::parse_m_command(lcws.substr(2, ((int)((cws).size())) - 2), combination);
        if (transcript == L"")
        {
            if (Hand::less_combination(current_combination, combination))
            {
                // Prepare for the next move
                current_combination = combination;
                alternate_current_move();
                send_next_move_prompts();
            }
            else
            {
                push_string_to_both(SERVER_PREFIX L" " COLOR_ESCAPE + client->get_nickname() + COLOR_ESCAPE L", текущая комбинация не хуже введённой вами."); // TODO: ENGLISH
            }
        }
        else
        {
            push_string_to_both(SERVER_PREFIX L" " COLOR_ESCAPE + client->get_nickname() + COLOR_ESCAPE L", " + transcript + L'.'); // TODO: ENGLISH
        }
    }

  }
  else{
    get_currently_not_moving_player()->
      push_string(SERVER_PREFIX L" It's not your move now!");
#ifdef SEND_COMMANDS_WHILE_OTHERS_MOVE
    get_currently_moving_player()->
      push_string(SERVER_PREFIX L" Your opponent "
                  L"tried to make a move: '" + command + L"'.");
#endif
  }
}

void Game::finish(const RoundResult& res){
  log("Finishing the game...\n");
  //TODO: Process the LOSE of the client (send messages, update ratings).

  first_player->set_in_game(false);
  second_player->set_in_game(false);
  first_player->set_state(WAIT_ENTER_GAME);
  second_player->set_state(WAIT_ENTER_GAME);


  finished = true;
};

void Game::send_next_move_prompts(){
  wchar_t buf[100];
  swprintf(buf, (wstring() + SERVER_PREFIX L" %" COLOR_ESCAPE L"%ls%" COLOR_ESCAPE L", you have %u seconds to move.").c_str(),
                  get_currently_moving_player()->get_nickname().c_str(),
                  (unsigned int) (MOVE_TIMEOUT / 1000));
    get_currently_moving_player()->
      push_string(buf);
    get_currently_not_moving_player()->
      push_string(SERVER_PREFIX L" Waiting your opponent to move...");
}

wstring cards_to_string(vector<CARD_TYPE> &cards){
  wchar_t str[BUFFER_SIZE];
  wchar_t* ptr = str;
  swprintf(ptr, CARDS_PREFIX L"%01u", (unsigned) cards.size());
  ptr += 7;
  for(int i = 0; i < ((int)(cards.size())); i++){
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
void Game::send_card_messages_to_one_player(Client* client){
  push_client_string_to_client(L"Cards:", client);
  client->push_string(cards_to_string(first_player_cards) + COLOR_ESCAPE + first_player->get_nickname() + COLOR_ESCAPE L": ");
  client->push_string(cards_to_string(second_player_cards) + COLOR_ESCAPE + second_player->get_nickname() + COLOR_ESCAPE L": ");
}
void Game::send_card_messages_to_both_players(){
  log("Sending card messages to both players\n");
  send_card_messages_to_one_player(first_player);
  send_card_messages_to_one_player(second_player);
}
void Game::send_card_numbers_to_one_player(Client* client, const wstring& zero_line, const wstring& first_line, const wstring& second_line)
{
    push_client_string_to_client(zero_line, client);
    push_client_string_to_client(first_line, client);
    push_client_string_to_client(second_line, client);
}
void Game::send_card_numbers_to_both_players(const RoundResult& res)
{
    log("Sending card numbers to both players\n");
    std::wstring zero_line, first_line, second_line;
    zero_line = L"Number of cards:";

    first_line = COLOR_ESCAPE + first_player->get_nickname() + COLOR_ESCAPE L": ";
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

    second_line = COLOR_ESCAPE + second_player->get_nickname() + COLOR_ESCAPE L": ";
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

    send_card_numbers_to_one_player(first_player, zero_line, first_line, second_line);
    send_card_numbers_to_one_player(second_player, zero_line, first_line, second_line);
}

void Game::send_card_messages_to_owners(){
  log("Sending card messages\n");
  //TODO: Send real cards according to current game state.
  //      Card message format:
  //      cards:<number_of_cards>:<suit>,<value>[:<suit>,<value>]<message>
  //      where cards - message keyword,
  //            <number_of_cards> - integer, number of suit-value pairs
  //                                in the message,
  //            <suit> - integer from 0 to 3,
  //            <value> - single char '2' - '9', '0', 'J', 'Q', 'K' or 'A'
  //            <message> - string to be printed BEFORE cards

  first_player->push_string(cards_to_string(first_player_cards));
  second_player->push_string(cards_to_string(second_player_cards));
}

//client is a loser of a round
//void Game::send_round_result_messages(Client* client){
//  wchar_t message_first[BUFFER_SIZE], message_second[BUFFER_SIZE];
//
//  if(client->get_id() == first_player->get_id()){
//    //First player lost round
//    swprintf(message_first, SERVER_PREFIX L" %s: %u cards :(",
//            first_player->get_nickname().c_str(),
//            6/*put new number of cards here*/);
//    swprintf(message_first, SERVER_PREFIX L" %s: %u cards",
//            second_player->get_nickname().c_str(),
//            5/*put new number of cards here*/);
//  }
//  else if(client->get_id() == first_player->get_id()){
//    //Second player lost round
//    swprintf(message_first, SERVER_PREFIX L" %s: %u cards",
//            first_player->get_nickname().c_str(),
//            5/*put new number of cards here*/);
//    swprintf(message_first, SERVER_PREFIX L" %s: %u cards :(",
//            second_player->get_nickname().c_str(),
//            6/*put new number of cards here*/);
//  }
//  else
//    return;
//
//  push_client_string_to_both(wstring(message_first));
//  push_client_string_to_both(wstring(message_second));
//}

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
