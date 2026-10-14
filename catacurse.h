#ifndef __CATACURSE__
#define __CATACURSE__
// SDL2-based replacement for the curses / catacurse layer.
// Same API as the old Windows catacurse.h, but no windows.h needed.
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>

typedef int chtype;
typedef unsigned short attr_t;

#define __CHARTEXT	0x000000ff
#define __NORMAL	0x00000000
#define __STANDOUT	0x00000100
#define __UNDERSCORE	0x00000200
#define __REVERSE	0x00000400
#define __BLINK		0x00000800
#define __DIM		0x00001000
#define __BOLD		0x00002000
#define __BLANK		0x00004000
#define __PROTECT	0x00008000
#define __ALTCHARSET	0x00010000
#define __COLOR		0x03fe0000
#define __ATTRIBUTES	0x03ffff00

// One character cell: unicode char, foreground index (0-15, 8+ = bold), background index (0-7)
typedef struct {
  unsigned short ch;
  unsigned char fg;
  unsigned char bg;
} cursecell;

typedef struct {
  int x;          // left side of window (on screen)
  int y;          // top side of window
  int width;
  int height;
  int FG;         // current foreground color (0-7)
  int BG;         // current background color (0-7)
  bool bold;      // current bold state
  bool inuse;
  bool draw;
  int cursorx;
  int cursory;
  cursecell *cells;   // width*height
  bool *touched;      // per line: changed since last refresh
} WINDOW;

#define	A_NORMAL	__NORMAL
#define	A_STANDOUT	__STANDOUT
#define	A_UNDERLINE	__UNDERSCORE
#define	A_REVERSE	__REVERSE
#define	A_BLINK		__BLINK
#define	A_DIM		__DIM
#define	A_BOLD		__BOLD
#define	A_BLANK		__BLANK
#define	A_PROTECT	__PROTECT
#define	A_ALTCHARSET	__ALTCHARSET
#define	A_ATTRIBUTES	__ATTRIBUTES
#define	A_CHARTEXT	__CHARTEXT
#define	A_COLOR		__COLOR

#define	COLOR_BLACK	0x00
#define	COLOR_RED	0x01
#define	COLOR_GREEN	0x02
#define	COLOR_YELLOW	0x03
#define	COLOR_BLUE	0x04
#define	COLOR_MAGENTA	0x05
#define	COLOR_CYAN	0x06
#define	COLOR_WHITE	0x07

#define	COLOR_PAIR(n)	((((u_int32_t)n) << 17) & A_COLOR)

#define    KEY_MIN        0x101
#define    KEY_BREAK      0x101
#define    KEY_DOWN       0x102
#define    KEY_UP         0x103
#define    KEY_LEFT       0x104
#define    KEY_RIGHT      0x105
#define    KEY_HOME       0x106
#define    KEY_BACKSPACE  0x107

#define stdscr 0

#define getmaxyx(w, y, x)  (y = getmaxy(w), x = getmaxx(w))

#define	ERR	(-1)
#define	OK	(0)

// Screen size (fixed, like the original Windows version)
extern int LINES;
extern int COLS;

WINDOW *newwin(int nlines, int ncols, int begin_y, int begin_x);
int delwin(WINDOW *win);
int wborder(WINDOW *win, chtype ls, chtype rs, chtype ts, chtype bs, chtype tl, chtype tr, chtype bl, chtype br);
int wrefresh(WINDOW *win);
int refresh(void);
int getch(void);
int mvwprintw(WINDOW *win, int y, int x, const char *fmt, ...);
int mvprintw(int y, int x, const char *fmt, ...);
int werase(WINDOW *win);
int start_color(void);
int init_pair(short pair, short f, short b);
int wmove(WINDOW *win, int y, int x);

int clear(void);
int erase(void);
int endwin(void);
int mvwaddch(WINDOW *win, int y, int x, const chtype ch);
int wclear(WINDOW *win);
int wprintw(WINDOW *win, const char *fmt, ...);
WINDOW *initscr(void);
int noecho(void);
int cbreak(void);
int keypad(WINDOW *faux, bool bf);
int curs_set(int visibility);
int mvaddch(int y, int x, const chtype ch);
int wattron(WINDOW *win, int attrs);
int wattroff(WINDOW *win, int attrs);
int attron(int attrs);
int attroff(int attrs);
int waddch(WINDOW *win, const chtype ch);
int printw(const char *fmt,...);
int getmaxx(WINDOW *win);
int getmaxy(WINDOW *win);
int move(int y, int x);
void timeout(int delay);
#endif
