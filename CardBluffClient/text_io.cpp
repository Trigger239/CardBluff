#include <windows.h>
#include <locale>
#include <string>
#include <cstdio>
#include <cwchar>
#include <vector>

#include "text_io.h"

using namespace std;

wchar_t *wcstok_r(wchar_t *str, const wchar_t *delim, wchar_t **nextp)
{
    wchar_t *ret;

    if (str == NULL)
    {
        str = *nextp;
    }

    str += wcsspn(str, delim);

    if (*str == L'\0')
    {
        return NULL;
    }

    ret = str;

    str += wcscspn(str, delim);

    if (*str)
    {
        *str++ = L'\0';
    }

    *nextp = str;

    return ret;
}

#if defined(__GNUC__) && (__GNUC___ < 6 || (__GNUC__ == 6 && __GNUC_MINOR__ <= 3))
// this is for gcc <= 6.3

typedef struct _CONSOLE_FONT_INFOEX
{
    ULONG cbSize;
    DWORD nFont;
    COORD dwFontSize;
    UINT  FontFamily;
    UINT  FontWeight;
    WCHAR FaceName[LF_FACESIZE];
}CONSOLE_FONT_INFOEX, *PCONSOLE_FONT_INFOEX;
//the function declaration begins
#ifdef __cplusplus
extern "C" {
#endif
BOOL WINAPI SetCurrentConsoleFontEx(HANDLE hConsoleOutput, BOOL bMaximumWindow, PCONSOLE_FONT_INFOEX
lpConsoleCurrentFontEx);
BOOL WINAPI GetCurrentConsoleFontEx(HANDLE hConsoleOutput, BOOL bMaximumWindow, PCONSOLE_FONT_INFOEX
lpConsoleCurrentFontEx);
#ifdef __cplusplus
}
#endif

#endif //gcc <= 6.3

#ifdef USE_MUTEX
HANDLE curses_mutex;
#endif // USE_MUTEX

WINDOW* input_win;
WINDOW* output_win;

unsigned int max_input_length;

//returns true on success
bool set_console_font(const wchar_t* font)
{
  HANDLE StdOut = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_FONT_INFOEX info;
  memset(&info, 0, sizeof(CONSOLE_FONT_INFOEX));
  info.cbSize = sizeof(CONSOLE_FONT_INFOEX);              // prevents err=87 below
  if (GetCurrentConsoleFontEx(StdOut, FALSE, &info))
  {
    info.FontFamily   = FF_DONTCARE;
    info.dwFontSize.X = 0;  // leave X as zero
    info.dwFontSize.Y = 15;
    info.FontWeight   = 400;
    wcscpy(info.FaceName, font);
    if (SetCurrentConsoleFontEx(StdOut, FALSE, &info))
    {
      return true;
    }
    else
      return false;
  }
  return false;
}

int rows, cols;
int output_win_py;

int output_win_refresh(WINDOW* w){
  return prefresh(w, (output_win_py = max(getcury(w) - rows + INPUT_WINDOW_ROWS + 1, 0)), 0, 0, 0, rows - INPUT_WINDOW_ROWS - 1, cols - 1);
}

bool console_init(void){
  //setlocale(LC_ALL, "ru_RU.utf8");
  setlocale(LC_ALL, "");
  //SetConsoleCP(65001);
  if(!set_console_font(L"Lucida Console"))
    return false;

  if(!SetConsoleTitleW(L"CardBluff"))
    return false;

#ifdef USE_MUTEX
  curses_mutex = CreateMutex(NULL, FALSE, NULL);
  if(curses_mutex == INVALID_HANDLE_VALUE)
    return false;
#endif // USE_MUTEX

  initscr();

  cbreak();             // Immediate key input
  nonl();               // Get return key
  timeout(0);           // Non-blocking input
  keypad(stdscr, 1);    // Fix keypad
  noecho();             // No automatic printing
  curs_set(0);          // Hide real cursor
  intrflush(stdscr, 0); // Avoid potential graphical issues
  leaveok(stdscr, 1);   // Don't care where cursor is left

  if(has_colors()){
    start_color();
    init_pair(COLOR_INPUT_ECHO, COLOR_CYAN, COLOR_BLACK);
    init_pair(COLOR_INPUT_CURSOR, COLOR_BLACK, COLOR_WHITE);
    init_pair(COLOR_MESSAGE_SERVER, COLOR_YELLOW, COLOR_BLACK);
    init_pair(COLOR_HEARTS, COLOR_RED, COLOR_BLACK);
    init_pair(COLOR_DIAMONDS, COLOR_YELLOW, COLOR_BLACK);
    init_pair(COLOR_SPADES, COLOR_GREEN, COLOR_BLACK);
    init_pair(COLOR_CLUBS, COLOR_BLUE, COLOR_BLACK);
    init_color(COLOR_MAGENTA, 1000, 500, 0);
    init_pair(COLOR_MESSAGE_HIGHLIGHT, COLOR_MAGENTA, COLOR_BLACK);
  }

  getmaxyx(stdscr, rows, cols);
  max_input_length = cols - 2;

  output_win_py = 0;
  //output_win = newwin(rows - INPUT_WINDOW_ROWS, cols, 0, 0);
  output_win = newpad(OUTPUT_WINDOW_ROWS, cols);
  refresh();
  wattron(output_win, A_BOLD);
  scrollok(output_win, true);
  output_win_refresh(output_win);

  input_win = newwin(INPUT_WINDOW_ROWS, cols, rows - INPUT_WINDOW_ROWS, 0);
  refresh();
  wattron(input_win, A_BOLD);
  use_win(input_win, [&](WINDOW* w){
            if(wclear(w) == ERR) return ERR;
            if(box(w, 0, 0) == ERR) return ERR;
            return wrefresh(w);
          });

  refresh();

  return true;
}

int use_win(WINDOW *win, std::function<int(WINDOW*)> cb_func){
  int ret;
#ifdef USE_MUTEX
  WaitForSingleObject(curses_mutex, INFINITE);
#endif // USE_MUTEX
  ret = cb_func(win);
#ifdef USE_MUTEX
  ReleaseMutex(curses_mutex);
#endif // USE_MUTEX
  return ret;
}

int output_win_addwstr(const wchar_t* str){
  return use_win(output_win, [&](WINDOW* w){int res = waddwstr(w, str); output_win_refresh(w); return res;});
}

int input_win_addwstr(const wchar_t* str){
  return use_win(input_win, [&](WINDOW* w){int res = waddwstr(w, str); wrefresh(w); return res;});
}

int win_wprintw(WINDOW* win, const char* format, ...){
  int ret;
  va_list args;
  va_start(args, format);
  ret = use_win(win, [&](WINDOW* w){int res = vwprintw(w, (const char*) format, args); wrefresh(w); return res;});
  va_end(args);
  return ret;
}

int win_print_with_highlight(WINDOW* win, const wchar_t* str, int default_color = 0){
  bool highlight_on = false;
  bool backslash_found = false;
  while(*str){
    if(backslash_found){
      if(*str != L'\\' && *str != COLOR_ESCAPE)
        if(waddwstr(win, L"\\") == ERR) return ERR;
      if(waddnwstr(win, str, 1) == ERR) return ERR;
      backslash_found = false;
    }
    else if(*str == L'\\'){
      backslash_found = true;
    }
    else if(*str == COLOR_ESCAPE){
      highlight_on = !highlight_on;
      if(highlight_on){
        if(default_color && (wattroff(win, COLOR_PAIR(default_color)) == ERR)) return ERR;
        if(wattron(win, COLOR_PAIR(COLOR_MESSAGE_HIGHLIGHT)) == ERR) return ERR;
      }
      else{
        if(wattroff(win, COLOR_PAIR(COLOR_MESSAGE_HIGHLIGHT)) == ERR) return ERR;
        if(default_color && (wattron(win, COLOR_PAIR(default_color)) == ERR)) return ERR;
      }
    }
    else{
      if(waddnwstr(win, str, 1) == ERR) return ERR;
    }
    str++;
  }
  if(backslash_found)
    if(waddwstr(win, L"\\") == ERR) return ERR;
  if(highlight_on){
    if(wattroff(win, COLOR_PAIR(COLOR_MESSAGE_HIGHLIGHT)) == ERR) return ERR;
    if(default_color && wattron(win, COLOR_PAIR(default_color)) == ERR) return ERR;
  }
  return OK;
}

bool win_get_wstr(WINDOW* input_win, WINDOW* output_win,
                  std::wstring &str, bool hide_input,
                  bool* input_ready){
  //*_terminate = false;

  static std::wstring input_buffer;
  static unsigned int cursor = 0;
  bool string_ready = false;

  bool need_update = true;
  bool ret = true;

  do{
  //while(true){
    wchar_t c;
    //int res = getch((wint_t*) &c);
    c = getch();

    if(c != (wchar_t) ERR){
      need_update = true;

      if(!(c & KEY_CODE_YES) && iswprint(c)) {
        if(input_buffer.size() < max_input_length)
          input_buffer.insert(cursor++, 1, c);
      }

      switch(c){
      case KEY_LEFT:
        if(cursor > 0)
          cursor--;
        break;

      case KEY_RIGHT:
        if(cursor < input_buffer.size())
          cursor++;
        break;

      case KEY_HOME:
      case KEY_A1:
        cursor = 0;
        break;

      case KEY_END:
      case KEY_C1:
        cursor = input_buffer.size();
        break;

      case L'\t':
        break;

      case PADSLASH:
        if(input_buffer.size() < max_input_length)
          input_buffer.insert(cursor++, 1, L'/');
        break;

      case PADPLUS:
        if(input_buffer.size() < max_input_length)
          input_buffer.insert(cursor++, 1, L'+');
        break;

      case PADMINUS:
        if(input_buffer.size() < max_input_length)
          input_buffer.insert(cursor++, 1, L'-');
        break;

      case PADSTAR:
        if(input_buffer.size() < max_input_length)
          input_buffer.insert(cursor++, 1, L'*');
        break;

      case PADSTOP:
        if(input_buffer.size() < max_input_length)
          input_buffer.insert(cursor++, 1, L'.');
        break;

      case KEY_BACKSPACE:
      case 127:
      case 8:
        if(cursor <= 0){
          break;
        }
        cursor--;
        // Fall-through
      case KEY_DC:
        if(cursor < input_buffer.size()){
          input_buffer.erase(cursor, 1);
        }
        break;

      case PADENTER:
      case KEY_ENTER:
      case L'\r':
      case L'\n':
        str = input_buffer;
        string_ready = true;
        *input_ready = true;
        break;

      case KEY_UP:
      case KEY_A2:
        use_win(output_win, [&](WINDOW* w){
                  //int begy = getbegy(w);
                  if(output_win_py <= 0)
                    return OK;
                  output_win_py--;
                  return prefresh(w, output_win_py, 0, 0, 0, rows - INPUT_WINDOW_ROWS - 1, cols - 1);
                });
        break;

      case KEY_DOWN:
      case KEY_C2:
        use_win(output_win, [&](WINDOW* w){
                  if(output_win_py >= getcury(w) - rows + INPUT_WINDOW_ROWS + 1)
                    return OK;
                  output_win_py++;
                  return prefresh(w, output_win_py, 0, 0, 0, rows - INPUT_WINDOW_ROWS - 1, cols - 1);
                });
        break;

      case CTL_HOME:
      case CTL_PAD7:
        use_win(output_win, [&](WINDOW* w){
                  output_win_py = 0;
                  return prefresh(w, output_win_py, 0, 0, 0, rows - INPUT_WINDOW_ROWS - 1, cols - 1);
                });
        break;

      case CTL_END:
      case CTL_PAD1:
        use_win(output_win, [&](WINDOW* w){
                  output_win_py = getcury(w) - rows + INPUT_WINDOW_ROWS + 1;
                  return prefresh(w, output_win_py, 0, 0, 0, rows - INPUT_WINDOW_ROWS - 1, cols - 1);
                });
        break;

      case KEY_PPAGE:
      case KEY_A3:
        use_win(output_win, [&](WINDOW* w){
                output_win_py = max(output_win_py - rows + INPUT_WINDOW_ROWS, 0);
                  return prefresh(w, output_win_py, 0, 0, 0, rows - INPUT_WINDOW_ROWS - 1, cols - 1);
                });
        break;

      case KEY_NPAGE:
      case KEY_C3:
        use_win(output_win, [&](WINDOW* w){
                output_win_py = min(output_win_py + rows - INPUT_WINDOW_ROWS, getcury(w) - rows + INPUT_WINDOW_ROWS + 1);
                  return prefresh(w, output_win_py, 0, 0, 0, rows - INPUT_WINDOW_ROWS - 1, cols - 1);
                });
        break;
      }
    }
    else{
      Sleep(10);
    }

    if(!string_ready && need_update){
      if(use_win(input_win, [&](WINDOW* w){
          if(wclear(w) == ERR) return ERR;
          if(box(w, 0, 0) == ERR) return ERR;
          if(hide_input){
            if(mvwaddwstr(w, 1, 1, std::wstring(input_buffer.size(), L'*').c_str()) == ERR)
              return ERR;
          }
          else{
            if(mvwaddwstr(w, 1, 1, input_buffer.c_str()) == ERR)
              return ERR;
          }
          if(mvwchgat(w, 1, cursor + 1, 1, COLOR_PAIR(COLOR_INPUT_CURSOR), 0, nullptr) == ERR) return ERR;
          return wrefresh(w);
        }) == ERR)
      {
        ret = false;
        break;
      }
      need_update = false;
    }

    if(string_ready){
      if(use_win(input_win, [&](WINDOW* w){
          if(wclear(w) == ERR) return ERR;
          if(box(w, 0, 0) == ERR) return ERR;
          return wrefresh(w);
        }) == ERR)
      {
        ret = false;
        break;
      }

      if(hide_input)
        input_buffer = std::wstring(input_buffer.size(), L'*');

      input_buffer += L"\n";

      if(use_win(output_win, [&](WINDOW* w){
          if(wattron(w, COLOR_PAIR(COLOR_INPUT_ECHO)) == ERR) return ERR;
          win_print_with_highlight(w, input_buffer.c_str(), COLOR_INPUT_ECHO);
//          if(waddwstr(w, input_buffer.c_str()) == ERR) return ERR;
          if(wattroff(w, COLOR_PAIR(COLOR_INPUT_ECHO)) == ERR) return ERR;
          return output_win_refresh(w);
        }) == ERR){
        ret = false;
      }
      break;
    }
  }
  while(0);

  if(string_ready){
    input_buffer = L"";
    cursor = 0;
  }

  return ret;
}

int output_win_addwstr_colored(const wchar_t* str){

  if(wcsncmp(str, SERVER_PREFIX, wcslen(SERVER_PREFIX)) == 0){
    return use_win(output_win, [&](WINDOW* w){
              if(wattron(w, COLOR_PAIR(COLOR_MESSAGE_SERVER)) == ERR) return ERR;
              if(waddwstr(w, SERVER_PREFIX) == ERR) return ERR;
              if(wattroff(w, COLOR_PAIR(COLOR_MESSAGE_SERVER)) == ERR) return ERR;
              if(win_print_with_highlight(w, str + wcslen(SERVER_PREFIX)) == ERR) return ERR;
              if(waddwstr(w, L"\n") == ERR) return ERR;
              return output_win_refresh(w);
            });
  }
  if(wcsncmp(str, CARDS_PREFIX, wcslen(CARDS_PREFIX)) == 0){
    std::vector<std::pair<unsigned int, wchar_t>> cards;
    unsigned int card_number, suit;
    wchar_t value;
    wchar_t *tok, *st;

    wchar_t* str_buf = new wchar_t[wcslen(str) + 1];
    wcscpy(str_buf, str);

    if((tok = wcstok_r(str_buf, L":", &st)) == nullptr){ //prefix
      delete[] str_buf;
      return ERR;
    }
    tok = wcstok_r(nullptr, L":", &st); //card number
    if(tok == nullptr ||
       swscanf(tok, L"%u", &card_number) != 1){
      delete[] str_buf;
      return ERR;
    }
    for(unsigned int i = 0; i < card_number; i++){
      if(i != card_number - 1)
        tok = wcstok_r(nullptr, L":", &st);
      else
        tok = st;
      if(tok == nullptr ||
         swscanf(tok, L"%u,%lc", &suit, &value) != 2){
        return ERR;
        delete[] str_buf;
      }
      cards.push_back(std::make_pair(suit, value));
    }

    tok += 3;

    delete[] str_buf;
    const wchar_t* pref_str = str + (tok - str_buf);

    return use_win(output_win, [&](WINDOW* w){
              if(wattron(w, COLOR_PAIR(COLOR_MESSAGE_SERVER)) == ERR) return ERR;
              if(waddwstr(w, SERVER_PREFIX L" ") == ERR) return ERR;
              if(wattroff(w, COLOR_PAIR(COLOR_MESSAGE_SERVER)) == ERR) return ERR;
              if(win_print_with_highlight(w, pref_str) == ERR) return ERR;
              for(auto card: cards){
                if(waddnwstr(w, &card.second, 1) == ERR) return ERR;
                switch(card.first){
                case HEARTS:
                  if(wattron(w, COLOR_PAIR(COLOR_HEARTS)) == ERR) return ERR;
                  if(waddnwstr(w, HEARTS_CHAR, 1) == ERR) return ERR;
                  if(wattroff(w, COLOR_PAIR(COLOR_HEARTS)) == ERR) return ERR;
                  break;

                case DIAMONDS:
                  if(wattron(w, COLOR_PAIR(COLOR_DIAMONDS)) == ERR) return ERR;
                  if(waddnwstr(w, DIAMONDS_CHAR, 1) == ERR) return ERR;
                  if(wattroff(w, COLOR_PAIR(COLOR_DIAMONDS)) == ERR) return ERR;
                  break;

                case SPADES:
                  if(wattron(w, COLOR_PAIR(COLOR_SPADES)) == ERR) return ERR;
                  if(waddnwstr(w, SPADES_CHAR, 1) == ERR) return ERR;
                  if(wattroff(w, COLOR_PAIR(COLOR_SPADES)) == ERR) return ERR;
                  break;

                case CLUBS:
                  if(wattron(w, COLOR_PAIR(COLOR_CLUBS)) == ERR) return ERR;
                  if(waddnwstr(w, CLUBS_CHAR, 1) == ERR) return ERR;
                  if(wattroff(w, COLOR_PAIR(COLOR_CLUBS)) == ERR) return ERR;
                  break;
                }
                if(&card != &cards.back()) if(waddwstr(w, L", ") == ERR) return ERR;
              }
              if(waddnwstr(w, L"\n", 1) == ERR) return ERR;
              return output_win_refresh(w);
            });
  }
  if(wcsncmp(str, ERROR_PREFIX, wcslen(ERROR_PREFIX)) == 0){
    return use_win(output_win, [&](WINDOW* w){
              if(wattron(w, COLOR_PAIR(COLOR_MESSAGE_ERROR)) == ERR) return ERR;
              if(waddwstr(w, str) == ERR) return ERR;
              if(wattroff(w, COLOR_PAIR(COLOR_MESSAGE_SERVER)) == ERR) return ERR;
              if(waddwstr(w, L"\n") == ERR) return ERR;
              return output_win_refresh(w);
            });
  }
  else if(wcsncmp(str, USER_PREFIX, wcslen(USER_PREFIX)) == 0){
    return use_win(output_win, [&](WINDOW* w){
              if(win_print_with_highlight(w, str + wcslen(USER_PREFIX)) == ERR) return ERR;
              if(waddwstr(w, L"\n") == ERR) return ERR;
              return output_win_refresh(w);
            });
  }
  else{
    return use_win(output_win, [&](WINDOW* w){
              if(waddwstr(w, L"> ") == ERR) return ERR;
              if(waddwstr(w, str) == ERR) return ERR;
              if(waddwstr(w, L"\n") == ERR) return ERR;
              return output_win_refresh(w);
            });
  }
}
