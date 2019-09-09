#ifndef TEXT_IO_H_INCLUDED
#define TEXT_IO_H_INCLUDED

#include <functional>

#undef MOUSE_MOVED
#define PDC_WIDE
#define NCURSES_WIDECHAR 1
#include "pdcurses/curses.h"

#define COLOR_INPUT_ECHO 2
#define COLOR_INPUT_CURSOR 3
#define COLOR_MESSAGE_SERVER 4
#define COLOR_HEARTS 5
#define COLOR_DIAMONDS 6
#define COLOR_SPADES 7
#define COLOR_CLUBS 8
#define COLOR_MESSAGE_ERROR 5
#define COLOR_MESSAGE_HIGHLIGHT 9

#define SERVER_PREFIX L"SERVER:"
#define CARDS_PREFIX L"CARDS:"
#define USER_PREFIX L"USER "
#define ERROR_PREFIX L"ERROR:"
#define COLOR_ESCAPE L'%'

typedef enum
{
	HEARTS		= 0,
	DIAMONDS	= 1,
	SPADES		= 2,
	CLUBS		= 3
} suit_t;

#define HEARTS_CHAR L"\x2665"
#define DIAMONDS_CHAR L"\x2666"
#define SPADES_CHAR L"\x2660"
#define CLUBS_CHAR L"\x2663"

#define INPUT_WINDOW_ROWS 3
#define OUTPUT_WINDOW_ROWS 10000

#define MIN_ROWS (INPUT_WINDOW_ROWS + 7)
#define MIN_COLS 10

//#define USE_MUTEX

extern WINDOW* input_win;
extern WINDOW* output_win;

bool console_init(void);

int use_win(WINDOW *win, std::function<int(WINDOW*)> cb_func);
int output_win_addwstr(const wchar_t* str);
int win_wprintw(WINDOW* win, const char* format, ...);
bool win_get_wstr(WINDOW* input_win, WINDOW* output_win,
                  std::wstring &str, bool hide_input,
                  bool* input_ready);

int output_win_addwstr_colored(const wchar_t* str);
void check_and_process_resize(void);

#endif // TEXT_IO_H_INCLUDED

