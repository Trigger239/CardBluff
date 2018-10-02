#include "client.h"

#include "util/strlcpy.h"

int Client::wait_mutex(){
  return WaitForSingleObject(mutex, INFINITE);
}

int Client::wait_mutex(HANDLE _mutex){
  return WaitForSingleObject(_mutex, INFINITE);
}

template<typename T>
void Client::set_var(T& var, T value){
  wait_mutex();
  var = value;
  ReleaseMutex(mutex);
}

template<typename T>
T Client::get_var(T& var){
  wait_mutex();
  T value = var;
  ReleaseMutex(mutex);
  return value;
}

Client::Client(const SOCKET& socket,
               const std::string& nickname,
               long long id)
  : socket(socket)
  , nickname(nickname)
  , id(id)
  {
    mutex = CreateMutex(NULL, FALSE, NULL);
    reconnect_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    terminate_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    waiting_reconnect = false;
    state = WAIT_NICKNAME;
    game = nullptr;
    opponent = nullptr;
    send_queue_mutex = CreateMutex(NULL, FALSE, NULL);
    finding_duel = false;
    in_game = false;
  };

Client::Client(const SOCKET& socket)
  : socket(socket)
  {
    nickname = "";
    id = 0;
    mutex = CreateMutex(NULL, FALSE, NULL);
    send_queue_mutex = CreateMutex(NULL, FALSE, NULL);
    reconnect_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    terminate_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    waiting_reconnect = false;
    finding_duel = false;
    in_game = false;
    authorized = false;
    state = WAIT_NICKNAME;
    game = nullptr;
    opponent = nullptr;
  };

Client::~Client(){
  wait_mutex();
  Game* g = get_game();
  if(g != nullptr){
    set_game(nullptr);
    delete g;

    Client* opp = get_opponent();
    if(opp != nullptr){
      opp->set_game(nullptr);
      opp->set_opponent(nullptr);
    }
  }
  CloseHandle(mutex);
  CloseHandle(send_queue_mutex);
  CloseHandle(reconnect_event);
  CloseHandle(terminate_event);
  CloseHandle(send_thread);
  CloseHandle(receive_thread);
}

HANDLE Client::_get_mutex(){
  return mutex;
}

HANDLE Client::get_terminate_event(){
  return terminate_event;
}

HANDLE Client::get_reconnect_event(){
  return reconnect_event;
}

void Client::set_waiting_reconnect(bool _waiting_reconnect){
  set_var(waiting_reconnect, _waiting_reconnect);
}

bool Client::get_waiting_reconnect(){
  return get_var(waiting_reconnect);
}

void Client::set_send_thread(HANDLE thread){
  set_var(send_thread, thread);
}

HANDLE Client::get_send_thread(){
  return get_var(send_thread);
}

void Client::set_receive_thread(HANDLE thread){
  set_var(receive_thread, thread);
}

HANDLE Client::get_receive_thread(){
  return get_var(receive_thread);
}

void Client::set_socket(SOCKET _socket){
  set_var(socket, _socket);
}

SOCKET Client::get_socket(){
  return get_var(socket);
}

int Client::receive_data(char* data, int data_size, bool* _terminate){
  if(_terminate == nullptr)
    return recv(get_socket(), data, data_size, 0);
  *_terminate = false;

  int res;
  while((res = recv(get_socket(), data, data_size, 0)) == SOCKET_ERROR){
    if(WSAGetLastError() != WSAEWOULDBLOCK)
      return SOCKET_ERROR;
    if(WaitForSingleObject(terminate_event, 0) == WAIT_OBJECT_0){
      *_terminate = true;
      return 0;
    }
    Sleep(10);
  }
  return res;
}

int Client::send_data(const char* data, int data_size, bool* _terminate){
  if(_terminate == nullptr)
    return send(get_socket(), data, data_size, 0);
  *_terminate = false;
  int res;
  while((res = send(get_socket(), data, data_size, 0)) == SOCKET_ERROR){
    if(WSAGetLastError() != WSAEWOULDBLOCK)
      return SOCKET_ERROR;
    if(WaitForSingleObject(terminate_event, 0) == WAIT_OBJECT_0){
      *_terminate = true;
      return 0;
    }
    Sleep(10);
  }
  return res;
}

void Client::push_string(std::string str, bool* _terminate){
  if(_terminate == nullptr){
    WaitForSingleObject(send_queue_mutex, INFINITE);
    send_queue.push(str);
    ReleaseMutex(send_queue_mutex);
    return;
  }

  *_terminate = false;

  if(wait_mutex(send_queue_mutex) == WAIT_OBJECT_0 + 1){
    *_terminate = true;
    return;
  }
  send_queue.push(str);
  ReleaseMutex(send_queue_mutex);
}

void Client::push_string(bool* _terminate, const char* format, ...){
  va_list va;
  char c_str[1024];
  std::string str;
  va_start(va, format);
  vsprintf(c_str, format, va);
  va_end(va);
  str.assign(c_str);
  push_string(str, _terminate);
}

bool Client::queue_is_empty(bool* _terminate){
  bool ret;

  if(_terminate == nullptr){
    WaitForSingleObject(send_queue_mutex, INFINITE);
    ret = send_queue.empty();
    ReleaseMutex(send_queue_mutex);
    return ret;
  }

  *_terminate = false;

  if(wait_mutex(send_queue_mutex) == WAIT_OBJECT_0 + 1){
    *_terminate = true;
    return false;
  }
  ret = send_queue.empty();
  ReleaseMutex(send_queue_mutex);
  return ret;
}

int Client::send_from_queue(bool* _terminate){
  int ret;
  if(_terminate == nullptr){
    WaitForSingleObject(send_queue_mutex, INFINITE);
    if(send_queue.empty()){
      ReleaseMutex(send_queue_mutex);
      return -239;
    }

    char send_buffer[SEND_BUFFER_SIZE];
    strlcpy(send_buffer, send_queue.front().c_str(), SEND_BUFFER_SIZE);

    ret = send_data(send_buffer, SEND_BUFFER_SIZE, nullptr);

    if(ret != SOCKET_ERROR) send_queue.pop();
    ReleaseMutex(send_queue_mutex);
    return ret;
  }

  *_terminate = false;

  if(wait_mutex(send_queue_mutex) == WAIT_OBJECT_0 + 1){
    *_terminate = true;
    return 0;
  }

  if(send_queue.empty()){
    ReleaseMutex(send_queue_mutex);
    return -239;
  }

  char send_buffer[SEND_BUFFER_SIZE];
  strlcpy(send_buffer, send_queue.front().c_str(), SEND_BUFFER_SIZE);

  ret = send_data(send_buffer, SEND_BUFFER_SIZE, _terminate);
  if(ret != SOCKET_ERROR && !(*_terminate))
    send_queue.pop();
  ReleaseMutex(send_queue_mutex);
  return ret;
}

void Client::set_nickname(const std::string& _nickname){
  set_var(nickname, _nickname);
}

void Client::set_nickname(const char* _nickname){
  std::string buf(_nickname);
  set_nickname(buf);
}

std::string Client::get_nickname(){
  return get_var(nickname);
}

void Client::set_id(long long _id){
  set_var(id, _id);
}

long long Client::get_id(){
  return get_var(id);
}

void Client::set_state(ClientState _state){
  set_var(state, _state);
}

ClientState Client::get_state(){
  return get_var(state);
}

void Client::set_game(Game* _game){
  set_var(game, _game);
}

Game* Client::get_game(){
  return get_var(game);
}

void Client::set_opponent(Client* _opponent){
  set_var(opponent, _opponent);
}

Client* Client::get_opponent(){
  return get_var(opponent);
}

void Client::set_finding_duel(bool _finding_duel){
  set_var(finding_duel, _finding_duel);
}

bool Client::get_finding_duel(){
  return get_var(finding_duel);
}

void Client::set_in_game(bool _in_game){
  set_var(in_game, _in_game);
}

bool Client::get_in_game(){
  return get_var(in_game);
}

void Client::set_authorized(bool _authorized){
  set_var(authorized, _authorized);
}

bool Client::get_authorized(){
  return get_var(authorized);
}

Game* Client::enter_game(Client* _opponent){
  //if(_terminate == nullptr){
    if(get_in_game())
      return get_game();

    set_in_game(true);
    set_opponent(_opponent);
    set_state(IN_GAME);
    Game* g = new Game(this, opponent, FIRST_PLAYER_MOVE);
    set_game(g);

    opponent->set_in_game(true);
    opponent->set_opponent(this);
    opponent->set_state(IN_GAME);
    opponent->set_game(g);

    return get_game();
  //}
//
//  bool res = get_in_game(_terminate);
//  if(*_terminate)
//    return nullptr;
//  if(res)
//    return get_game(_terminate);
//
//  set_in_game(true, _terminate);
//  if(*_terminate)
//    return nullptr;
//  set_opponent(opponent, _terminate);
//  if(*_terminate)
//    return nullptr;
//  set_state(IN_GAME, _terminate);
//  if(*_terminate)
//    return nullptr;
//  long long _id = get_id(_terminate);
//  if(*_terminate)
//    return nullptr;
//  long long _opp_id = opponent->get_id(_terminate);
//  if(*_terminate)
//    return nullptr;
//
//  game_t* g = new game_t(_id, _opp_id, 0);
//  set_game(g, _terminate);
//  if(*_terminate){
//    delete g;
//    return nullptr;
//  }
//
//  opponent->set_in_game(true, _terminate);
//  if(*_terminate){
//    delete g;
//    return nullptr;
//  }
//  opponent->set_opponent(this, _terminate);
//  if(*_terminate){
//    delete g;
//    return nullptr;
//  }
//  opponent->set_state(IN_GAME, _terminate);
//  if(*_terminate){
//    delete g;
//    return nullptr;
//  }
//  opponent->set_game(g, _terminate);
//  if(*_terminate){
//    delete g;
//    return nullptr;
//  }
//
//  return get_game(_terminate);
}

void Client::close_socket(){
  closesocket(socket);
}

void Client::copy_strings(Client* dest_client, bool* _terminate){
  if(_terminate == nullptr){
    WaitForSingleObject(send_queue_mutex, INFINITE);
    while(!send_queue.empty()){
      dest_client->push_string(send_queue.front());
      send_queue.pop();
    }
    ReleaseMutex(send_queue_mutex);
    return;
  }

  *_terminate = false;

  if(wait_mutex(send_queue_mutex) == WAIT_OBJECT_0 + 1){
    *_terminate = true;
    return;
  }
  while(!send_queue.empty()){
      dest_client->push_string(send_queue.front(), _terminate);
      if(*_terminate){
        ReleaseMutex(send_queue_mutex);
        return;
      }
      send_queue.pop();
    }
  ReleaseMutex(send_queue_mutex);
}
