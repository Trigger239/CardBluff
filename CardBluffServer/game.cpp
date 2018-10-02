#include "game.h"
#include "common.h"

Game::Game(Client* first_player,
           Client* second_player,
           CurrentMove current_move)
  : first_player(first_player)
  , second_player(second_player)
  , current_move(current_move){
    terminate_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    move_event = CreateEvent(NULL, TRUE, FALSE, NULL);

    InitializeCriticalSection(&finish_critical_section);
    InitializeCriticalSection(&make_move_critical_section);

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
      push_string(nullptr, "SERVER: You haven't done your move in time.");

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
  send_card_messages(_terminate);
  if(_terminate != nullptr && *_terminate)
    return;
  send_next_move_prompts(_terminate);
  if(_terminate != nullptr && *_terminate)
    return;
}

bool Game::is_valid_command(const std::string& command){
  return true;
}

void Game::make_move(Client* client, const std::string& command, bool* _terminate){
  *_terminate = false;

  while(!TryEnterCriticalSection(&make_move_critical_section)){
    if(WaitForSingleObject(terminate_event, 10)){
      *_terminate = true;
      return;
    }
  }

  if(!is_valid_command(command)){
    client->push_string(_terminate, "SERVER: Wrong command!");
    LeaveCriticalSection(&make_move_critical_section);
    return;
  }

  if(makes_current_move(client)){

    //Sending move command to currently not moving player
    get_currently_not_moving_player()->
      push_string(_terminate, "%s: %s",
                  get_currently_moving_player()->get_nickname().c_str(),
                  command.c_str());
    if(*_terminate){
      LeaveCriticalSection(&make_move_critical_section);
      return;
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
      push_string(_terminate, "SERVER: It's not your move now!");
    if(*_terminate){
      LeaveCriticalSection(&make_move_critical_section);
      return;
    }
    get_currently_moving_player()->
      push_string(_terminate, "SERVER: Your opponent "
                  "tried to make a move: '%s'.", command.c_str());
    if(*_terminate){
      LeaveCriticalSection(&make_move_critical_section);
      return;
    }
  }
  LeaveCriticalSection(&make_move_critical_section);
}

void Game::finish(Client* client, bool* _terminate){
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

  first_player->set_in_game(false);
  second_player->set_in_game(false);
  first_player->set_state(WAIT_ENTER_GAME);
  second_player->set_state(WAIT_ENTER_GAME);

  //TODO: Process the LOSE of the client (send messages, update ratings).
  //... (modify here)

  SetEvent(terminate_event);

  LeaveCriticalSection(&finish_critical_section);
};

void Game::send_next_move_prompts(bool* _terminate){
    if(_terminate != nullptr)
      *_terminate = false;
    get_currently_moving_player()->
      push_string(_terminate, "SERVER: %s, you have %u seconds to move.",
                  get_currently_moving_player()->get_nickname().c_str(),
                  (unsigned int) (MOVE_TIMEOUT / 1000));
    if(_terminate != nullptr && *_terminate){
      return;
    }
    get_currently_not_moving_player()->
      push_string(_terminate, "SERVER: Waiting your opponent to move...");
    if(_terminate != nullptr && *_terminate){
      return;
    }
}

void Game::send_card_messages(bool* _terminate){
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
    push_string(_terminate, "cards:5:0,9:0,A:3,Q:1,K:2,5");
  if(_terminate != nullptr && *_terminate){
    return;
  }

  second_player->
    push_string(_terminate, "cards:6:1,6:2,A:0,J:2,2:2,0:2,6");
  if(_terminate != nullptr && *_terminate){
    return;
  }
}

//client is a loser of a round
void Game::send_round_result_messages(Client* client, bool* _terminate){
  if(_terminate != nullptr)
    *_terminate = false;

  char message_first[256], message_second[256];

  //TODO: Send real number of cards

  if(client->get_id() == first_player->get_id()){
    //First player lost round
    sprintf(message_first, "SERVER: %s: %u cards :(",
            first_player->get_nickname().c_str(),
            6/*put new number of cards here*/);
    sprintf(message_first, "SERVER: %s: %u cards",
            second_player->get_nickname().c_str(),
            5/*put new number of cards here*/);
  }
  else if(client->get_id() == first_player->get_id()){
    //Second player lost round
    sprintf(message_first, "SERVER: %s: %u cards",
            first_player->get_nickname().c_str(),
            5/*put new number of cards here*/);
    sprintf(message_first, "SERVER: %s: %u cards :(",
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
