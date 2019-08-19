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
#define COLOR_HEARTS_DIAMONDS 5
#define COLOR_SPADES_CLUBS 6

#define SERVER_PREFIX L"SERVER:"
#define CARDS_PREFIX L"CARDS:"
#define USER_PREFIX L"USER "

typedef enum
{
	HEARTS		= 0,
	DIAMONDS	= 1,
	SPADES		= 2,
	CLUBS		= 3
} suit_t;

#define INPUT_WINDOW_ROWS 3

extern WINDOW* input_win;
extern WINDOW* output_win;

bool console_init(void);

int use_win(WINDOW *win, std::function<int(WINDOW*)> cb_func);
int win_addwstr(WINDOW* win, const wchar_t* str);
int win_wprintw(WINDOW* win, const char* format, ...);
bool win_get_wstr(WINDOW* input_win, WINDOW* output_win,
                  std::wstring &str, bool hide_input,
                  HANDLE terminate_event,
                  bool* _terminate = nullptr);

int win_addwstr_colored(WINDOW* win, wchar_t* str);

#endif // TEXT_IO_H_INCLUDED

