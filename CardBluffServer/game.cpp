#include "game.h"

#include <cstdio>
#include <cwchar>
#include <cwctype>

#include "common.h"
#include "ratings.h"

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
           Client* second_player)
  : finished(false)
  , first_player(first_player)
  , second_player(second_player)
  , gnr(chrono::high_resolution_clock::now().time_since_epoch().count())
  , logger(L"Game " + ptr_to_wstring(this))
  {
    thread_handle_ready_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    command_queue_mutex = CreateMutex(NULL, FALSE, NULL);

    first_player_card_number = START_CARD_NUMBER;
    second_player_card_number = START_CARD_NUMBER;

    first_move = (gnr() & 1) ? FIRST_PLAYER_MOVE : SECOND_PLAYER_MOVE;

    logger << L"New game: '" << first_player->get_nickname() << "' VS '" << second_player->get_nickname() << Logger::endline;
  };

Game::~Game(){
  CloseHandle(thread_handle_ready_event);
  CloseHandle(command_queue_mutex);
}

void Game::push_command(Client* client, const wstring& command){
  //logger(L"Pushing new command into queue: '" + command + L"'");
  WaitForSingleObject(command_queue_mutex, INFINITE);
  command_queue.emplace(client, command, current_move);
  ReleaseMutex(command_queue_mutex);
}

void Game::push_reconnect(Client* client){
  //logger(L"Pushing reconnect event of '" + client->get_nickname() + L"' into queue");
  WaitForSingleObject(command_queue_mutex, INFINITE);
  command_queue.emplace(client, L"", current_move, RECONNECT);
  ReleaseMutex(command_queue_mutex);
}

void Game::push_disconnect(Client* client){
  //logger(L"Pushing disconnect event of '" + client->get_nickname() + L"' into queue");
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

    long long tr;
    wstring tr_str;

    switch(cmd.type){
      case MOVE_COMMAND:
        make_move(cmd);
        break;

      case RECONNECT:
        logger(L"Reconnect event of '" + cmd.sender->get_nickname() + L"' found in queue");
        push_client_string_to_client(L"You are still in game!", cmd.sender);
        send_card_numbers_to_one_player(cmd.sender);
        cmd.sender->push_string(cards_to_string(cmd.sender == first_player ?
                                                first_player_cards : second_player_cards));

        tr = get_remaining_move_time() * 10.0;
        tr_str = ll_to_wstring(tr / 10) + L"." + ll_to_wstring(tr % 10);

        logger(L"Sending remaining time to '" + cmd.sender->get_nickname() + L"': " + tr_str + L"s");

        if(makes_current_move(cmd.sender))
          push_client_string_to_client(cmd.sender->get_nickname_with_color() + L", you have " +
                                       tr_str + L" second" + (tr > 10 ? L"s" : L"") + L" to move.", cmd.sender);
        else
          push_client_string_to_client(L"Waiting for your opponent to move...", cmd.sender);
        break;

      case DISCONNECT:
        logger(L"Disconnect event of '" + cmd.sender->get_nickname() + L"' found in queue");
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
    if(get_remaining_move_time(now) <= 0.0){
      logger(L"'" + get_currently_moving_player()->get_nickname() + L"' haven't done the move in time");

      push_client_string_to_client(get_currently_moving_player()->get_nickname_with_color() + L", you haven't done your move in time!", get_currently_moving_player());
      push_client_string_to_client(get_currently_moving_player()->get_nickname_with_color() + L" hasn't done the move in time!", get_currently_not_moving_player());

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

void Game::push_strings_to_both(const vector<wstring> &str){
  first_player->push_strings(str);
  second_player->push_strings(str);
}

void Game::start_round(){
  logger(L"Starting new round...");

  current_combination.clear();
  current_combination.push_back(NOTHING);
  last_move = L"";
  first_player_wants_tie = false;
  second_player_wants_tie = false;
  alternate_first_move();
  generate_cards();

  move_start_time = chrono::high_resolution_clock::now();
  logger << L"Round started at " << time_to_wstring(move_start_time) << Logger::endline;

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

void Game::generate_cards()
{
    logger << L"Generating cards (numbers: " << first_player_card_number << L", " << second_player_card_number << L")" << Logger::endline;
    vector<CARD_TYPE> my_cards = generate_shuffled_array_of_cards();
    first_player_cards.clear();
    second_player_cards.clear();
    copy(my_cards.begin(), my_cards.begin() + first_player_card_number, back_inserter(first_player_cards));
    copy(my_cards.begin() + first_player_card_number, my_cards.begin() + first_player_card_number + second_player_card_number, back_inserter(second_player_cards));
    my_cards.resize(first_player_card_number + second_player_card_number);
    union_of_cards = Hand(my_cards);
    logger << L"First player cards: " << cards_to_string(first_player_cards) << Logger::endline;
    logger << L"Second player cards: " << cards_to_string(second_player_cards) << Logger::endline;
}


void Game::player_loses_round(const CurrentMove& cur)
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
    report_round_results(res);
    if (FinishedGame(res))
        finish(res);
    else
        start_round();
}
void Game::tie_in_round()
{
    report_round_results(TIE_IN_ROUND);
    start_round();
}
void Game::report_round_results(const RoundResult& res)
{
    switch(res){
    case FIRST_PLAYER_LOST_ROUND:
      logger(L"'" + first_player->get_nickname() + L"' lost round");
      break;
    case SECOND_PLAYER_LOST_ROUND:
      logger(L"'" + second_player->get_nickname() + L"' lost round");
      break;
    case FIRST_PLAYER_LOST_GAME:
      logger(L"'" + first_player->get_nickname() + L"' lost game");
      break;
    case SECOND_PLAYER_LOST_GAME:
      logger(L"'" + second_player->get_nickname() + L"' lost game");
      break;
    case TIE_IN_ROUND:
      logger(L"Tie in round");
      break;
    case TIE_IN_GAME:
      logger(L"Tie in game");
      break;
    }
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
// TODO: use everywhere the four following functions
void Game::push_client_string_to_both(const wstring &str, Client* cl)
{
    wstring addend = cl ? USER_PREFIX + cl->get_nickname_with_color() + L":" : SERVER_PREFIX;
    push_string_to_both(addend + L" " + str);
}

void Game::push_client_strings_to_both(const vector<wstring> &str, Client* cl)
{
    wstring addend = cl ? USER_PREFIX + cl->get_nickname_with_color() + L":" : SERVER_PREFIX;
    vector<wstring> buf(str.size());
    for(size_t i = 0; i < str.size(); i++)
      buf[i] = addend + L" " + str[i];
    push_strings_to_both(buf);
}

void Game::push_client_string_to_client(const wstring &str, Client* receiver, Client* sender)
{
    wstring addend = sender ? USER_PREFIX + sender->get_nickname_with_color() + L":" : SERVER_PREFIX;
    receiver->push_string(addend + L" " + str);
}

void Game::push_client_strings_to_client(const vector<wstring> &str, Client* receiver, Client* sender)
{
    wstring addend = sender ? USER_PREFIX + sender->get_nickname_with_color() + L":" : SERVER_PREFIX;
    vector<wstring> buf(str.size());
    for(size_t i = 0; i < str.size(); i++)
      buf[i] = addend + L" " + str[i];
    receiver->push_strings(buf);
}

void Game::make_move(Command cmd){
  assert(cmd.type == MOVE_COMMAND);

  wstring command = cmd.cmd;
  Client* client = cmd.sender;
  std::wstring cws = remove_space_characters(cmd.cmd); // command_without_spaces
  std::wstring lcws;                         // lowered_command_without_spaces
  std::transform(cws.begin(), cws.end(), back_inserter(lcws), ::towlower);

  logger(L"Command from '" + client->get_nickname() + L"': '" + command + L"'");

  if(lcws == L"/concede"){
      logger(L"'" + client->get_nickname() + L"' conceded");

      push_client_string_to_client(lcws, client == first_player ? second_player : first_player, client);

      push_client_string_to_client(L"You conceded!", client);
      push_client_string_to_client(client->get_nickname_with_color() + L" conceded!",
                                   client == first_player ? second_player : first_player);

      report_round_results(client == first_player ? FIRST_PLAYER_LOST_GAME : SECOND_PLAYER_LOST_GAME);
      finish(client == first_player ? FIRST_PLAYER_LOST_GAME : SECOND_PLAYER_LOST_GAME);
      return;
  }
  if(lcws == L"/tie" || lcws == L"/draw"){
    logger(L"'" + client->get_nickname() + L"' wants to tie");
    push_client_string_to_client(lcws, (client == first_player) ? second_player : first_player, client);

    if(client == first_player)
      first_player_wants_tie = true;
    else
      second_player_wants_tie = true;

    if(first_player_wants_tie && second_player_wants_tie){
      logger(L"The both players want tie");

      push_client_string_to_both(L"Both you and your opponent agreed to a draw.");

      report_round_results(TIE_IN_GAME);
      finish(TIE_IN_GAME);
    }
    return;
  }
  if(lcws == L"/tr"){
      long long tr = get_remaining_move_time() * 10.0;
      wstring tr_str;
      tr_str = ll_to_wstring(tr / 10) + L"." + ll_to_wstring(tr % 10);

      logger(L"Sending remaining time to '" + client->get_nickname() + L"': " + tr_str + L"s");

      if(makes_current_move(client))
        push_client_string_to_client(client->get_nickname_with_color() + L", you have " +
                                     tr_str + L" second" + (tr > 10 ? L"s" : L"") + L" to move.", client);
      else
        push_client_string_to_client(get_currently_moving_player()->get_nickname_with_color() + L" has " +
                                     tr_str + L" second" + (tr > 10 ? L"s" : L"") + L" to move.", client);
      return;
  }
  if(lcws == L"/cards"){
    logger(L"Sending cards to '" + client->get_nickname() + L"'");
    client->push_string(cards_to_string(client == first_player ? first_player_cards : second_player_cards));
    return;
  }
  if(lcws == L"/lastmove" || lcws == L"/lm"){
    logger(L"Sending last move to '" + client->get_nickname() + L"'");
    if(last_move == L""){
      push_client_string_to_client(L"No moves have been made in this round yet.", client);
    }
    else{
      push_client_string_to_client(L"Last move: " + last_move + L" (" +
                                   get_currently_not_moving_player()->get_nickname_with_color() + L")",
                                   client);
    }
    return;
  }

  if(makes_current_move(client)){
    //Sending move command to currently not moving player

    if (lcws == L"/r")
    {
        logger(L"'" + client->get_nickname() + L"' revealed");
        push_client_string_to_client(lcws, get_currently_not_moving_player(), client);
        if (Hand::is_combination_nothing(current_combination))
            push_client_string_to_client(client->get_nickname_with_color() + L", запрещено вскрываться на первом ходу.", client); // TODO: ENGLISH
        else if (union_of_cards.check_combination(current_combination))
        {
            //push_client_string_to_client(cws, client, get_currently_not_moving_player());
            push_client_string_to_both(client->get_nickname_with_color() + L", здесь есть эта комбинация."); // TODO: ENGLISH
            player_loses_round(current_move);
        }
        else
        {
            //push_client_string_to_client(cws, client, get_currently_not_moving_player());
            push_client_string_to_both(client->get_nickname_with_color() + L", здесь нет этой комбинации."); // TODO: ENGLISH
            player_loses_round(negate_CurrentMove(current_move));
        }
    }
    else if (lcws == L"/b")
    {
        logger(L"'" + client->get_nickname() + L"' blocked");
        push_client_string_to_client(lcws, get_currently_not_moving_player(), client);

        if (Hand::is_combination_nothing(current_combination))
            push_client_string_to_client(client->get_nickname_with_color() + L", запрещено блокировать на первом ходу.", client); // TODO: ENGLISH
        else if (union_of_cards.is_best_combination(current_combination))
        {
            push_client_string_to_both(client->get_nickname_with_color() + L", это лучшая комбинация."); // TODO: ENGLISH
            tie_in_round();
        }
        else{
            if (union_of_cards.check_combination(current_combination))
            {
                push_client_string_to_both(client->get_nickname_with_color() + L", это не лучшая комбинация."); // TODO: ENGLISH
            }
            else
            {
                push_client_string_to_both(client->get_nickname_with_color() + L", этой комбинации здесь нет."); // TODO: ENGLISH
            }

            push_client_string_to_both(L"Best combination: " +
                                Hand::format_m_command(union_of_cards.find_best_combination()));
            player_loses_round(current_move);
        }
    }
    else if (wcsncmp(lcws.c_str(), L"/m", 2) == 0)
    {
        vector<int> combination;
        //push_client_string_to_client(cws, client, get_currently_not_moving_player());
        wstring transcript = Hand::parse_m_command(cws.substr(2, ((int)((cws).size())) - 2), combination);
        if (transcript == L"")
        {
            wstring cmd_formatted = cws.substr(0, 2) + L" " + cws.substr(2, ((int)((cws).size())) - 2);
            push_client_string_to_client(cmd_formatted, get_currently_not_moving_player(), client);

            if (Hand::less_combination(current_combination, combination))
            {
                move_start_time = cmd.t;
                last_move = cmd_formatted;
                first_player_wants_tie = false;
                second_player_wants_tie = false;

                // Prepare for the next move
                current_combination = combination;
                alternate_current_move();
                send_next_move_prompts();

                logger(L"Next move started at " + time_to_wstring(move_start_time));
            }
            else
            {
                logger(L"New combination is not better then current!");
                push_client_string_to_client(client->get_nickname_with_color() + L", текущая комбинация не хуже введённой вами.", client); // TODO: ENGLISH
            }
        }
        else
        {
            push_client_string_to_client(client->get_nickname_with_color() + L", " + escape_special_chars(transcript) + L".", client); // TODO: ENGLISH
        }
    }
    else
        push_client_string_to_client(command, get_currently_not_moving_player(), client);

  }
  else{
    push_client_string_to_client(L"It's not your move now!", get_currently_not_moving_player());
#ifdef SEND_COMMANDS_WHILE_OTHERS_MOVE
    push_client_string_to_client(L" Your opponent "
                                 L"tried to make a move: '" + command +
                                 L"'.", get_currently_moving_player());
#endif
  }
}

void Game::finish(const RoundResult& res){
  logger(L"Finishing the game...");
  //TODO: Process the LOSE of the client (send messages, update ratings).

  char* z_err_msg;
  long long delta; //added to first player
  bool rating_update_ok = false;
  long long first_player_rating;
  long long second_player_rating;

  if(res == FIRST_PLAYER_LOST_GAME){
    logger(L"Updating ratings...");
    if(update_ratings(db, second_player, first_player,
                      &second_player_rating, &first_player_rating,
                      &delta, &z_err_msg)){
      logger << L"Error updating ratings. SQLite error: " << z_err_msg << Logger::endline;
    }
    else{
      rating_update_ok = true;
      delta = -delta;
    }
  }
  else if(res == SECOND_PLAYER_LOST_GAME){
    logger(L"Updating ratings...");
    if(update_ratings(db, first_player, second_player,
                      &first_player_rating, &second_player_rating,
                      &delta, &z_err_msg)){
      logger << L"Error updating ratings. SQLite error: " << z_err_msg << Logger::endline;
    }
    else{
      rating_update_ok = true;
    }
  }
  if(rating_update_ok){
    logger(L"Ratings updated: '" +
           first_player->get_nickname() + L"'" + ll_to_wstring(delta, true) +
           L"=" + ll_to_wstring(first_player_rating) + L"; '" +
           second_player->get_nickname() + L"'" + ll_to_wstring(-delta, true) +
           L"=" + ll_to_wstring(second_player_rating));

    vector<wstring> buf(3);
    buf[0] = L"Ratings:";
    buf[1] = first_player->get_nickname_with_color() + L": " +
             ll_to_wstring(first_player_rating) + L" (" +
             ll_to_wstring(delta, true) + L")";
    buf[2] = second_player->get_nickname_with_color() + L": " +
             ll_to_wstring(second_player_rating) + L" (" +
             ll_to_wstring(-delta, true) + L")";

    push_client_strings_to_both(buf);
  }

  first_player->set_in_game(false);
  second_player->set_in_game(false);
  first_player->set_state(WAIT_ENTER_GAME);
  second_player->set_state(WAIT_ENTER_GAME);

  finished = true;
};

void Game::send_next_move_prompts(){
  push_client_string_to_client(get_currently_moving_player()->get_nickname_with_color() + L", you have " +
                               ll_to_wstring(MOVE_TIMEOUT + 0.5) + L" seconds o move.",
                               get_currently_moving_player());
  push_client_string_to_client(L"Waiting for your opponent to move...", get_currently_not_moving_player());
}

wstring Game::cards_to_string(vector<CARD_TYPE> &cards){
  wchar_t str[BUFFER_SIZE];
  wchar_t* ptr = str;
  swprintf(ptr, CARDS_PREFIX L"%u", (unsigned) cards.size());
  ptr += wcslen(ptr);
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
  client->push_string(cards_to_string(first_player_cards) + first_player->get_nickname_with_color() + L": ");
  client->push_string(cards_to_string(second_player_cards) + second_player->get_nickname_with_color() + L": ");
}
void Game::send_card_messages_to_both_players(){
  logger(L"Sending card messages to both players...");
  send_card_messages_to_one_player(first_player);
  send_card_messages_to_one_player(second_player);
}
void Game::send_card_numbers_to_one_player(Client* client)
{
  logger(L"Sending card numbers to '" + client->get_nickname() + L"'...");
  std::wstring zero_line, first_line, second_line;
  zero_line = L"Number of cards:";

  first_line = first_player->get_nickname_with_color() + L": " +
               ll_to_wstring(first_player_card_number);

  second_line = second_player->get_nickname_with_color() + L": " +
                ll_to_wstring(second_player_card_number);

  vector<wstring> buf(3);
  buf[0] = zero_line;
  buf[1] = first_line;
  buf[2] = second_line;
  push_client_strings_to_client(buf, client);
}
void Game::send_card_numbers_to_both_players(const RoundResult& res)
{
    logger(L"Sending card numbers to both players...");
    std::wstring zero_line, first_line, second_line;
    zero_line = L"Number of cards:";

    first_line = first_player->get_nickname_with_color() + L": ";
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

    second_line = second_player->get_nickname_with_color() + L": ";
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

    vector<wstring> buf(3);
    buf[0] = zero_line;
    buf[1] = first_line;
    buf[2] = second_line;
    push_client_strings_to_both(buf);
}

void Game::send_card_messages_to_owners(){
  logger(L"Sending card messages to owners...");
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

HANDLE Game::get_thread_handle_ready_event(){
  return thread_handle_ready_event;
}

void Game::set_thread(HANDLE _thread){
  thread = _thread;
}

void Game::set_db(sqlite3* _db){
  db = _db;
}

HANDLE Game::get_thread(){
  return thread;
}

CurrentMove negate_CurrentMove(const CurrentMove& my_move)
{
  if(my_move == FIRST_PLAYER_MOVE)
    return SECOND_PLAYER_MOVE;
  else if(my_move == SECOND_PLAYER_MOVE)
    return FIRST_PLAYER_MOVE;

  assert(false);
  return FIRST_PLAYER_MOVE;
}

void Game::alternate_current_move() {
  current_move = negate_CurrentMove(current_move);
}

void Game::alternate_first_move(){
  first_move = negate_CurrentMove(first_move);
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

double Game::get_remaining_move_time(chrono::high_resolution_clock::time_point now){
  return max(MOVE_TIMEOUT - chrono::duration_cast<chrono::duration<double>>(now - move_start_time).count(), 0.0);
}

Logger& Game::get_logger(){
  return logger;
}
