#ifndef COMMON_H
#define COMMON_H

#define RECEIVE_BUFFER_SIZE 256
#define SEND_BUFFER_SIZE 256

#define MAX_CLIENTS 50

#define PORT 2390
#define ALL_CARDS ((uint8_t)(52))
#define START_CARD_NUMBER ((uint8_t)(5))
#define EQUALITY_CARD_NUMBER ((uint8_t)(10))
#define TIE_CARD_NUMBER ((uint8_t)(26))

#define SERVER_PREFIX L"SERVER:"
#define CARDS_PREFIX L"CARDS:"
#define USER_PREFIX L"USER "
#define COLOR_ESCAPE L"%"

#include "climits"

#define DEFAULT_RATING 1200
#define MOVE_TIMEOUT 60.0
#define RECONNECT_TIMEOUT (3600 * 12 * 1000)

//#define SEND_COMMANDS_WHILE_OTHERS_MOVE

#define TOP_LINES_MAX 100
#define TOP_LINES_DEFAULT 10

#include "util/util.h"

#endif // COMMON_H
