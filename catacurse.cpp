// SDL2 implementation of the catacurse API (replaces the Windows version).
// Draws an 80x25 character grid with SDL2_ttf. Written in plain C++98.
#include "catacurse.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <deque>

#define SCR_COLS 80
#define SCR_ROWS 25

int LINES = SCR_ROWS;
int COLS  = SCR_COLS;

// 0-7 normal colors, 8-15 bold/bright versions
static const unsigned char PAL[16][3] = {
 {0,0,0},      {196,0,0},   {0,196,0},   {196,180,30},
 {0,0,196},    {196,0,180}, {0,170,200}, {196,196,196},
 {99,99,99},   {255,0,0},   {0,255,0},   {255,255,0},
 {100,100,255},{255,0,240}, {0,240,255}, {255,255,255}
};

struct glyph_t { SDL_Texture *tex; int w, h; };

static SDL_Window   *g_window = NULL;
static SDL_Renderer *g_renderer = NULL;
static TTF_Font     *g_font = NULL;
static int g_cw = 10, g_chh = 20;           // cell size in pixels
static cursecell g_screen[SCR_ROWS * SCR_COLS];
static std::map<unsigned short, glyph_t> g_glyphs;
static WINDOW *g_std = NULL;
static int g_timeout = -1;
static std::deque<int> g_keys;
static short g_pair_fg[512];
static short g_pair_bg[512];

static WINDOW *W(WINDOW *w) { return w ? w : g_std; }

static void blank_cell(cursecell &c) { c.ch = ' '; c.fg = 7; c.bg = 0; }

static glyph_t get_glyph(unsigned short ch)
{
 std::map<unsigned short, glyph_t>::iterator it = g_glyphs.find(ch);
 if (it != g_glyphs.end())
  return it->second;
 glyph_t g;
 g.tex = NULL; g.w = 0; g.h = 0;
 SDL_Color white = {255, 255, 255, 255};
 SDL_Surface *s = TTF_RenderGlyph_Blended(g_font, ch, white);
 if (s) {
  g.tex = SDL_CreateTextureFromSurface(g_renderer, s);
  g.w = s->w; g.h = s->h;
  SDL_FreeSurface(s);
 }
 g_glyphs[ch] = g;
 return g;
}

static void present()
{
 if (!g_renderer)
  return;
 SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
 SDL_RenderClear(g_renderer);
 for (int r = 0; r < SCR_ROWS; r++) {
  for (int c = 0; c < SCR_COLS; c++) {
   cursecell &cell = g_screen[r * SCR_COLS + c];
   SDL_Rect dst;
   dst.x = c * g_cw; dst.y = r * g_chh; dst.w = g_cw; dst.h = g_chh;
   if (cell.bg != 0) {
    const unsigned char *b = PAL[cell.bg & 7];
    SDL_SetRenderDrawColor(g_renderer, b[0], b[1], b[2], 255);
    SDL_RenderFillRect(g_renderer, &dst);
   }
   if (cell.ch != ' ' && cell.ch != 0) {
    glyph_t g = get_glyph(cell.ch);
    if (g.tex) {
     const unsigned char *f = PAL[cell.fg & 15];
     SDL_SetTextureColorMod(g.tex, f[0], f[1], f[2]);
     if (cell.ch >= 0x2500 && cell.ch <= 0x257F) {
      SDL_RenderCopy(g_renderer, g.tex, NULL, &dst);   // box drawing: fill the whole cell
     } else {
      SDL_Rect d2 = dst; d2.w = g.w; d2.h = g.h;
      SDL_RenderCopy(g_renderer, g.tex, NULL, &d2);
     }
    }
   }
  }
 }
 SDL_RenderPresent(g_renderer);
}

// chtype -> unicode char. ncurses ACS line characters have bit 0x400000 set
// and the low byte is the classic ACS letter (x = vertical, q = horizontal ...).
static unsigned short to_glyph(int ch)
{
 if (ch & 0x400000) {
  switch (ch & 0xff) {
   case 'x': return 0x2502;
   case 'q': return 0x2500;
   case 'l': return 0x250C;
   case 'k': return 0x2510;
   case 'm': return 0x2514;
   case 'j': return 0x2518;
   case 't': return 0x251C;
   case 'u': return 0x2524;
   case 'v': return 0x2534;
   case 'w': return 0x252C;
   case 'n': return 0x253C;
   default:  return '+';
  }
 }
 return (unsigned short)(ch & 0xff);
}

static void put(WINDOW *w, int y, int x, int ch)
{
 if (x < 0 || y < 0 || x >= w->width || y >= w->height)
  return;
 cursecell &c = w->cells[y * w->width + x];
 c.ch = to_glyph(ch);
 c.fg = (unsigned char)((w->FG & 7) | (w->bold ? 8 : 0));
 c.bg = (unsigned char)(w->BG & 7);
 w->touched[y] = true;
}

static void clear_window(WINDOW *w)
{
 for (int i = 0; i < w->width * w->height; i++)
  blank_cell(w->cells[i]);
 for (int y = 0; y < w->height; y++)
  w->touched[y] = true;
 w->cursorx = 0;
 w->cursory = 0;
}

static WINDOW *alloc_window(int nlines, int ncols, int y, int x)
{
 WINDOW *w = new WINDOW;
 w->x = x; w->y = y;
 w->width = ncols; w->height = nlines;
 w->FG = 7; w->BG = 0; w->bold = false;
 w->inuse = true; w->draw = false;
 w->cursorx = 0; w->cursory = 0;
 w->cells = new cursecell[ncols * nlines];
 w->touched = new bool[nlines];
 clear_window(w);
 return w;
}

static void handle_event(const SDL_Event &e)
{
 switch (e.type) {
  case SDL_QUIT:
   endwin();
   exit(0);
   break;
  case SDL_WINDOWEVENT:
   if (e.window.event == SDL_WINDOWEVENT_EXPOSED ||
       e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
       e.window.event == SDL_WINDOWEVENT_RESTORED)
    present();
   break;
  case SDL_TEXTINPUT:
   for (const char *p = e.text.text; *p; p++)
    if ((unsigned char)*p < 128)
     g_keys.push_back(*p);
   break;
  case SDL_KEYDOWN: {
   SDL_Keycode k = e.key.keysym.sym;
   bool numlock = (e.key.keysym.mod & KMOD_NUM) != 0;
   if ((e.key.keysym.mod & KMOD_CTRL) && k >= SDLK_a && k <= SDLK_z) {
    g_keys.push_back(k - SDLK_a + 1);
    break;
   }
   switch (k) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:  g_keys.push_back(10); break;
    case SDLK_ESCAPE:    g_keys.push_back(27); break;
    case SDLK_BACKSPACE: g_keys.push_back(127); break;
    case SDLK_TAB:       g_keys.push_back(9); break;
    case SDLK_UP:        g_keys.push_back(KEY_UP); break;
    case SDLK_DOWN:      g_keys.push_back(KEY_DOWN); break;
    case SDLK_LEFT:      g_keys.push_back(KEY_LEFT); break;
    case SDLK_RIGHT:     g_keys.push_back(KEY_RIGHT); break;
    case SDLK_HOME:      g_keys.push_back(KEY_HOME); break;
    case SDLK_KP_0: if (!numlock) g_keys.push_back('0'); break;
    case SDLK_KP_1: if (!numlock) g_keys.push_back('1'); break;
    case SDLK_KP_2: if (!numlock) g_keys.push_back('2'); break;
    case SDLK_KP_3: if (!numlock) g_keys.push_back('3'); break;
    case SDLK_KP_4: if (!numlock) g_keys.push_back('4'); break;
    case SDLK_KP_5: if (!numlock) g_keys.push_back('5'); break;
    case SDLK_KP_6: if (!numlock) g_keys.push_back('6'); break;
    case SDLK_KP_7: if (!numlock) g_keys.push_back('7'); break;
    case SDLK_KP_8: if (!numlock) g_keys.push_back('8'); break;
    case SDLK_KP_9: if (!numlock) g_keys.push_back('9'); break;
    default: break;
   }
   break;
  }
  default:
   break;
 }
}

static TTF_Font *open_font(int size)
{
 const char *paths[] = {
  getenv("CATA_FONT"),
  "font.ttf",
  "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
  "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
  "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
  "/usr/share/fonts/liberation/LiberationMono-Regular.ttf"
 };
 for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
  if (!paths[i])
   continue;
  TTF_Font *f = TTF_OpenFont(paths[i], size);
  if (f)
   return f;
 }
 return NULL;
}

WINDOW *initscr(void)
{
 if (g_std)
  return g_std;
 if (SDL_Init(SDL_INIT_VIDEO) != 0) {
  fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
  exit(1);
 }
 if (TTF_Init() != 0) {
  fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
  exit(1);
 }
 int size = 16;
 if (getenv("CATA_FONTSIZE"))
  size = atoi(getenv("CATA_FONTSIZE"));
 if (size < 6) size = 16;
 g_font = open_font(size);
 if (!g_font) {
  fprintf(stderr, "No monospace font found.\n"
                  "Install one (Manjaro: sudo pacman -S ttf-dejavu) or put a font.ttf\n"
                  "next to the game, or set CATA_FONT=/path/to/font.ttf\n");
  exit(1);
 }
 TTF_SizeUTF8(g_font, "M", &g_cw, &g_chh);
 if (g_cw < 1) g_cw = 10;
 if (g_chh < 1) g_chh = 20;

 g_window = SDL_CreateWindow("Cataclysm", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             SCR_COLS * g_cw, SCR_ROWS * g_chh, SDL_WINDOW_RESIZABLE);
 if (!g_window) {
  fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
  exit(1);
 }
 g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
 if (!g_renderer)
  g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
 if (!g_renderer) {
  fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
  exit(1);
 }
 SDL_RenderSetLogicalSize(g_renderer, SCR_COLS * g_cw, SCR_ROWS * g_chh);
 SDL_StartTextInput();

 for (int i = 0; i < SCR_ROWS * SCR_COLS; i++)
  blank_cell(g_screen[i]);
 for (int i = 0; i < 512; i++) { g_pair_fg[i] = 7; g_pair_bg[i] = 0; }

 g_std = alloc_window(SCR_ROWS, SCR_COLS, 0, 0);
 present();
 return g_std;
}

int endwin(void)
{
 for (std::map<unsigned short, glyph_t>::iterator it = g_glyphs.begin(); it != g_glyphs.end(); ++it)
  if (it->second.tex)
   SDL_DestroyTexture(it->second.tex);
 g_glyphs.clear();
 if (g_font)     { TTF_CloseFont(g_font); g_font = NULL; }
 if (g_renderer) { SDL_DestroyRenderer(g_renderer); g_renderer = NULL; }
 if (g_window)   { SDL_DestroyWindow(g_window); g_window = NULL; }
 TTF_Quit();
 SDL_Quit();
 return OK;
}

WINDOW *newwin(int nlines, int ncols, int begin_y, int begin_x)
{
 if (nlines <= 0) nlines = SCR_ROWS - begin_y;
 if (ncols <= 0)  ncols  = SCR_COLS - begin_x;
 if (nlines < 1) nlines = 1;
 if (ncols < 1)  ncols = 1;
 return alloc_window(nlines, ncols, begin_y, begin_x);
}

int delwin(WINDOW *win)
{
 if (!win)
  return ERR;
 delete [] win->cells;
 delete [] win->touched;
 if (win == g_std)
  g_std = NULL;
 delete win;
 return OK;
}

int wrefresh(WINDOW *win)
{
 win = W(win);
 if (!win)
  return ERR;
 bool changed = false;
 for (int y = 0; y < win->height; y++) {
  if (!win->touched[y])
   continue;
  win->touched[y] = false;
  changed = true;
  int sy = win->y + y;
  if (sy < 0 || sy >= SCR_ROWS)
   continue;
  for (int x = 0; x < win->width; x++) {
   int sx = win->x + x;
   if (sx < 0 || sx >= SCR_COLS)
    continue;
   g_screen[sy * SCR_COLS + sx] = win->cells[y * win->width + x];
  }
 }
 if (changed)
  present();
 return OK;
}

int refresh(void) { return wrefresh(g_std); }

int getch(void)
{
 // Like ncurses' wgetch(): implicitly refresh stdscr before waiting for a key.
 // (The help screens only call mvprintw + getch, without refresh().)
 wrefresh(g_std);
 SDL_Event e;
 Uint32 start = SDL_GetTicks();
 for (;;) {
  if (!g_keys.empty()) {
   int k = g_keys.front();
   g_keys.pop_front();
   return k;
  }
  if (g_timeout == 0) {
   while (SDL_PollEvent(&e))
    handle_event(e);
   if (g_keys.empty())
    return ERR;
   continue;
  }
  if (g_timeout < 0) {
   if (SDL_WaitEvent(&e))
    handle_event(e);
   continue;
  }
  Uint32 elapsed = SDL_GetTicks() - start;
  if (elapsed >= (Uint32)g_timeout)
   return ERR;
  if (SDL_WaitEventTimeout(&e, (int)((Uint32)g_timeout - elapsed)))
   handle_event(e);
 }
}

void timeout(int delay) { g_timeout = delay; }

int werase(WINDOW *win) { win = W(win); if (!win) return ERR; clear_window(win); return OK; }
int wclear(WINDOW *win) { return werase(win); }
int clear(void) { return werase(g_std); }
int erase(void) { return werase(g_std); }

int wmove(WINDOW *win, int y, int x)
{
 win = W(win);
 if (!win) return ERR;
 win->cursory = y;
 win->cursorx = x;
 return OK;
}
int move(int y, int x) { return wmove(g_std, y, x); }

int waddch(WINDOW *win, const chtype ch)
{
 win = W(win);
 if (!win) return ERR;
 int c = ch;
 if (!(c & 0x400000)) {
  if ((c & 0xff) == '\n') { win->cursorx = 0; win->cursory++; return OK; }
  if ((c & 0xff) == '\r') { win->cursorx = 0; return OK; }
  if ((c & 0xff) == '\t') {
   do { put(win, win->cursory, win->cursorx, ' '); win->cursorx++; } while (win->cursorx % 8);
   return OK;
  }
 }
 put(win, win->cursory, win->cursorx, c);
 win->cursorx++;
 if (win->cursorx >= win->width) {
  win->cursorx = 0;
  win->cursory++;
 }
 return OK;
}

int mvwaddch(WINDOW *win, int y, int x, const chtype ch)
{
 if (wmove(win, y, x) == ERR) return ERR;
 return waddch(win, ch);
}
int mvaddch(int y, int x, const chtype ch) { return mvwaddch(g_std, y, x, ch); }

static int vprint(WINDOW *win, const char *fmt, va_list ap)
{
 char buf[4096];
 vsnprintf(buf, sizeof(buf), fmt, ap);
 for (const char *p = buf; *p; p++)
  waddch(win, (unsigned char)*p);
 return OK;
}

int wprintw(WINDOW *win, const char *fmt, ...)
{
 va_list ap; va_start(ap, fmt);
 int r = vprint(win, fmt, ap);
 va_end(ap);
 return r;
}
int mvwprintw(WINDOW *win, int y, int x, const char *fmt, ...)
{
 if (wmove(win, y, x) == ERR) return ERR;
 va_list ap; va_start(ap, fmt);
 int r = vprint(win, fmt, ap);
 va_end(ap);
 return r;
}
int printw(const char *fmt, ...)
{
 va_list ap; va_start(ap, fmt);
 int r = vprint(g_std, fmt, ap);
 va_end(ap);
 return r;
}
int mvprintw(int y, int x, const char *fmt, ...)
{
 if (wmove(g_std, y, x) == ERR) return ERR;
 va_list ap; va_start(ap, fmt);
 int r = vprint(g_std, fmt, ap);
 va_end(ap);
 return r;
}

int wborder(WINDOW *win, chtype ls, chtype rs, chtype ts, chtype bs, chtype tl, chtype tr, chtype bl, chtype br)
{
 win = W(win);
 if (!win) return ERR;
 if (!ls) ls = 0x400000 | 'x';
 if (!rs) rs = 0x400000 | 'x';
 if (!ts) ts = 0x400000 | 'q';
 if (!bs) bs = 0x400000 | 'q';
 if (!tl) tl = 0x400000 | 'l';
 if (!tr) tr = 0x400000 | 'k';
 if (!bl) bl = 0x400000 | 'm';
 if (!br) br = 0x400000 | 'j';
 int w = win->width, h = win->height;
 for (int y = 1; y < h - 1; y++) { put(win, y, 0, ls); put(win, y, w - 1, rs); }
 for (int x = 1; x < w - 1; x++) { put(win, 0, x, ts); put(win, h - 1, x, bs); }
 put(win, 0, 0, tl);
 put(win, 0, w - 1, tr);
 put(win, h - 1, 0, bl);
 put(win, h - 1, w - 1, br);
 return OK;
}

int start_color(void) { return OK; }

int init_pair(short pair, short f, short b)
{
 if (pair < 0 || pair >= 512)
  return ERR;
 g_pair_fg[pair] = f;
 g_pair_bg[pair] = b;
 return OK;
}

int wattron(WINDOW *win, int attrs)
{
 win = W(win);
 if (!win) return ERR;
 if (attrs & A_COLOR) {
  int p = (attrs & A_COLOR) >> 17;
  win->FG = g_pair_fg[p] & 7;
  win->BG = g_pair_bg[p] & 7;
 }
 if (attrs & A_BOLD)
  win->bold = true;
 return OK;
}

int wattroff(WINDOW *win, int attrs)
{
 win = W(win);
 if (!win) return ERR;
 if (attrs & A_COLOR) {
  win->FG = 7;
  win->BG = 0;
 }
 if (attrs & A_BOLD)
  win->bold = false;
 return OK;
}
int attron(int attrs)  { return wattron(g_std, attrs); }
int attroff(int attrs) { return wattroff(g_std, attrs); }

int getmaxx(WINDOW *win) { win = W(win); return win ? win->width : 0; }
int getmaxy(WINDOW *win) { win = W(win); return win ? win->height : 0; }

// Dummy functions kept for compatibility
int noecho(void) { return OK; }
int cbreak(void) { return OK; }
int keypad(WINDOW *faux, bool bf) { (void)faux; (void)bf; return OK; }
int curs_set(int visibility) { (void)visibility; return OK; }
