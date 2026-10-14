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
#include <string>
#include <vector>
#include <utility>
#include <ctype.h>
#include <math.h>

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
static std::string g_toast;          // short hint line shown over the bottom row
static Uint32 g_toast_until = 0;
static bool g_first_run = false;     // no controls.cfg existed yet
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

// ---------------------------------------------------------------------------
// On-screen keyboard overlay (a controller can type any key with it).
// Drawn on top of the game in present(); the game keeps running underneath.
// ---------------------------------------------------------------------------
static bool g_vk_open = false;
static int  g_vk_r = 0, g_vk_c = 0;
typedef std::pair<std::string, int> vkitem;
static std::vector<std::vector<vkitem> > g_vk_rows;

static void vk_build()
{
 g_vk_rows.clear();
 std::vector<vkitem> r;
 r.push_back(vkitem("enter", 10));
 r.push_back(vkitem("esc", 27));
 r.push_back(vkitem("tab", 9));
 r.push_back(vkitem("space", ' '));
 r.push_back(vkitem("backspace", 127));
 g_vk_rows.push_back(r);
 r.clear();
 r.push_back(vkitem("up", KEY_UP));
 r.push_back(vkitem("down", KEY_DOWN));
 r.push_back(vkitem("left", KEY_LEFT));
 r.push_back(vkitem("right", KEY_RIGHT));
 g_vk_rows.push_back(r);
 r.clear();
 for (int ch = 33; ch < 127; ch++) {
  char b[2] = { (char)ch, 0 };
  r.push_back(vkitem(std::string(b), ch));
  if (r.size() == 16) { g_vk_rows.push_back(r); r.clear(); }
 }
 if (!r.empty())
  g_vk_rows.push_back(r);
}

static void buf_text(cursecell *buf, int row, int col, const std::string &s, int fg, int bg)
{
 for (size_t i = 0; i < s.size(); i++) {
  int c = col + (int)i;
  if (row < 0 || row >= SCR_ROWS || c < 0 || c >= SCR_COLS)
   continue;
  cursecell &cc = buf[row * SCR_COLS + c];
  cc.ch = (unsigned char)s[i];
  cc.fg = (unsigned char)fg;
  cc.bg = (unsigned char)bg;
 }
}

static void vk_compose(cursecell *buf)
{
 if (g_vk_rows.empty())
  vk_build();
 int nrows = (int)g_vk_rows.size();
 int top = SCR_ROWS - nrows - 2;
 for (int r = top; r < SCR_ROWS; r++)
  for (int c = 0; c < SCR_COLS; c++)
   blank_cell(buf[r * SCR_COLS + c]);
 buf_text(buf, top, 2, "ON-SCREEN KEYBOARD", 11, 0);
 for (int i = 0; i < nrows; i++) {
  int width = (i < 2) ? 11 : 4;
  for (size_t j = 0; j < g_vk_rows[i].size(); j++) {
   bool sel = (i == g_vk_r && (int)j == g_vk_c);
   std::string label = g_vk_rows[i][j].first;
   if (sel) buf_text(buf, top + 1 + i, 2 + (int)j * width, " " + label + " ", 0, 7);
   else     buf_text(buf, top + 1 + i, 2 + (int)j * width, " " + label, 7, 0);
  }
 }
 buf_text(buf, SCR_ROWS - 1, 2, "D-pad/stick: move   A: type   X: backspace   B or START: close", 8, 0);
}

static void present()
{
 if (!g_renderer)
  return;
 static cursecell comp[SCR_ROWS * SCR_COLS];
 const cursecell *src = g_screen;
 if (g_vk_open) {
  memcpy(comp, g_screen, sizeof(comp));
  vk_compose(comp);
  src = comp;
 }
 SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
 SDL_RenderClear(g_renderer);
 for (int r = 0; r < SCR_ROWS; r++) {
  for (int c = 0; c < SCR_COLS; c++) {
   const cursecell &cell = src[r * SCR_COLS + c];
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
 if (g_toast_until && SDL_GetTicks() < g_toast_until) {
  int row = SCR_ROWS - 1;
  int start = (SCR_COLS - (int)g_toast.size()) / 2;
  if (start < 0) start = 0;
  for (int c = 0; c < SCR_COLS; c++) {
   SDL_Rect dst;
   dst.x = c * g_cw; dst.y = row * g_chh; dst.w = g_cw; dst.h = g_chh;
   SDL_SetRenderDrawColor(g_renderer, 230, 230, 230, 255);
   SDL_RenderFillRect(g_renderer, &dst);
   int idx = c - start;
   if (idx >= 0 && idx < (int)g_toast.size() && g_toast[idx] != ' ') {
    glyph_t g = get_glyph((unsigned char)g_toast[idx]);
    if (g.tex) {
     SDL_SetTextureColorMod(g.tex, 0, 0, 0);
     SDL_Rect d2 = dst; d2.w = g.w; d2.h = g.h;
     SDL_RenderCopy(g_renderer, g.tex, NULL, &d2);
    }
   }
  }
 }
 SDL_RenderPresent(g_renderer);
}

static void show_toast(const std::string &text, int ms)
{
 g_toast = text;
 g_toast_until = SDL_GetTicks() + ms;
 present();
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


// ---------------------------------------------------------------------------
// Game controller support. Everything is configurable in controls.cfg
// (created on first run, reload with F5 while playing).
// ---------------------------------------------------------------------------
static const char *DEFAULT_CFG =
"# Controller configuration for Cataclysm.\n"
"# Format:   BUTTON = key        or      MODIFIER+BUTTON = key\n"
"#\n"
"# Buttons:    A B X Y BACK START GUIDE LB RB LT RT LSTICK_CLICK RSTICK_CLICK\n"
"#             DPAD_UP DPAD_DOWN DPAD_LEFT DPAD_RIGHT\n"
"# Sticks:     LSTICK_N LSTICK_NE LSTICK_E LSTICK_SE LSTICK_S LSTICK_SW LSTICK_W LSTICK_NW\n"
"#             (same with RSTICK_ for the right stick)\n"
"# Modifiers:  LT RT LB RB  (hold one while pressing another input, e.g. LT+A = g)\n"
"# Keys:       a single character (a  5  ?  <  >  E)  or one of:\n"
"#             enter esc tab space backspace up down left right none\n"
"#             keyboard (opens the on-screen keyboard)  settings (opens this menu)\n"
"#\n"
"# Edit this file, then press F5 in the game to reload it.\n"
"\n"
"repeat_delay = 300\n"
"repeat_rate = 120\n"
"deadzone = 0.5\n"
"\n"
"A = enter\n"
"B = esc\n"
"X = i\n"
"Y = e\n"
"LB = <\n"
"RB = >\n"
"BACK = ?\n"
"START = keyboard\n"
"\n"
"DPAD_UP = k\n"
"DPAD_DOWN = j\n"
"DPAD_LEFT = h\n"
"DPAD_RIGHT = l\n"
"\n"
"LSTICK_N = k\n"
"LSTICK_NE = u\n"
"LSTICK_E = l\n"
"LSTICK_SE = n\n"
"LSTICK_S = j\n"
"LSTICK_SW = b\n"
"LSTICK_W = h\n"
"LSTICK_NW = y\n"
"\n"
"# Examples (remove the # to use):\n"
"# LT+A = g\n"
"# LT+B = d\n"
"# RT+X = w\n"
;

static const char *PAD_NAMES[17] = {
 "A", "B", "X", "Y", "BACK", "GUIDE", "START", "LSTICK_CLICK", "RSTICK_CLICK",
 "LB", "RB", "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT", "LT", "RT"
};
static const char *DIR_NAMES[8] = { "E", "NE", "N", "NW", "W", "SW", "S", "SE" };

static std::map<std::string, int> g_bind;
static int   g_rep_delay = 300;
static int   g_rep_rate  = 120;
static float g_deadzone  = 0.5f;
static bool  g_btn[17];
static int   g_ax[6];
static int   g_stick_dir[2] = { -1, -1 };

struct repeater_t { bool active; std::string name; Uint32 next; };
static repeater_t g_rep_dpad;
static repeater_t g_rep_stick[2];

// in-game settings menu state
struct ui_in { std::string name; std::string full; };
static bool g_ui_active = false;
static bool g_open_ui = false;
static int  g_ui_mod_pending = -1;
static std::deque<ui_in> g_ui_q;

static std::string str_trim(const std::string &s)
{
 size_t a = 0, b = s.size();
 while (a < b && isspace((unsigned char)s[a])) a++;
 while (b > a && isspace((unsigned char)s[b - 1])) b--;
 return s.substr(a, b - a);
}

static std::string str_lower(std::string s)
{
 for (size_t i = 0; i < s.size(); i++) s[i] = (char)tolower((unsigned char)s[i]);
 return s;
}

static std::string str_upper(std::string s)
{
 for (size_t i = 0; i < s.size(); i++) s[i] = (char)toupper((unsigned char)s[i]);
 return s;
}

static bool parse_key(const std::string &v, int &key)
{
 std::string l = str_lower(v);
 if (l == "none")      { key = -1; return true; }
 if (l == "keyboard")  { key = -3; return true; }
 if (l == "settings")  { key = -4; return true; }
 if (l == "enter")     { key = 10; return true; }
 if (l == "esc" || l == "escape") { key = 27; return true; }
 if (l == "tab")       { key = 9; return true; }
 if (l == "space")     { key = ' '; return true; }
 if (l == "backspace") { key = 127; return true; }
 if (l == "up")        { key = KEY_UP; return true; }
 if (l == "down")      { key = KEY_DOWN; return true; }
 if (l == "left")      { key = KEY_LEFT; return true; }
 if (l == "right")     { key = KEY_RIGHT; return true; }
 if (v.size() == 1)    { key = (unsigned char)v[0]; return true; }
 return false;
}

static void parse_cfg(const std::string &text)
{
 size_t pos = 0;
 int lineno = 0;
 while (pos <= text.size()) {
  size_t eol = text.find('\n', pos);
  if (eol == std::string::npos) eol = text.size();
  std::string line = str_trim(text.substr(pos, eol - pos));
  pos = eol + 1;
  lineno++;
  if (line.empty() || line[0] == '#')
   continue;
  size_t eq = line.find('=');
  if (eq == std::string::npos) {
   fprintf(stderr, "controls.cfg line %d: missing '='\n", lineno);
   continue;
  }
  std::string left = str_trim(line.substr(0, eq));
  std::string right = str_trim(line.substr(eq + 1));
  std::string lname = str_lower(left);
  if (lname == "repeat_delay") { g_rep_delay = atoi(right.c_str()); continue; }
  if (lname == "repeat_rate")  { g_rep_rate  = atoi(right.c_str()); continue; }
  if (lname == "deadzone")     { g_deadzone  = (float)atof(right.c_str()); continue; }
  int key;
  if (!parse_key(right, key)) {
   fprintf(stderr, "controls.cfg line %d: unknown key '%s'\n", lineno, right.c_str());
   continue;
  }
  g_bind[str_upper(left)] = key;
 }
}

static void load_controls()
{
 g_bind.clear();
 g_rep_delay = 300; g_rep_rate = 120; g_deadzone = 0.5f;
 std::string text;
 FILE *f = fopen("controls.cfg", "rb");
 if (f) {
  char buf[1024];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
   text.append(buf, n);
  fclose(f);
 } else {
  text = DEFAULT_CFG;
  g_first_run = true;
  FILE *w = fopen("controls.cfg", "wb");
  if (w) {
   fwrite(DEFAULT_CFG, 1, strlen(DEFAULT_CFG), w);
   fclose(w);
  }
 }
 parse_cfg(text);
 if (g_rep_delay < 0) g_rep_delay = 0;
 if (g_rep_rate < 16) g_rep_rate = 16;
 if (g_deadzone < 0.1f) g_deadzone = 0.1f;
 if (g_deadzone > 0.95f) g_deadzone = 0.95f;
}

static bool lookup_binding(const std::string &name, int &key)
{
 static const int mod_ids[4] = { 15, 16, 9, 10 };   // LT RT LB RB
 for (int i = 0; i < 4; i++) {
  if (!g_btn[mod_ids[i]] || name == PAD_NAMES[mod_ids[i]])
   continue;
  std::map<std::string, int>::iterator it =
   g_bind.find(std::string(PAD_NAMES[mod_ids[i]]) + "+" + name);
  if (it != g_bind.end()) { key = it->second; return true; }
 }
 std::map<std::string, int>::iterator it = g_bind.find(name);
 if (it != g_bind.end()) { key = it->second; return true; }
 return false;
}

enum { NAV_NONE = 0, NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT, NAV_OK, NAV_CANCEL,
       NAV_DEL, NAV_CAPTURED, NAV_TEXT };

static int name_to_nav(const std::string &n)
{
 if (n == "DPAD_UP"    || n == "LSTICK_N") return NAV_UP;
 if (n == "DPAD_DOWN"  || n == "LSTICK_S") return NAV_DOWN;
 if (n == "DPAD_LEFT"  || n == "LSTICK_W") return NAV_LEFT;
 if (n == "DPAD_RIGHT" || n == "LSTICK_E") return NAV_RIGHT;
 if (n == "A") return NAV_OK;
 if (n == "B") return NAV_CANCEL;
 if (n == "X") return NAV_DEL;
 return NAV_NONE;
}

// Input while the on-screen keyboard is open.
static void vk_input(const std::string &name)
{
 int key;
 if (lookup_binding(name, key) && key == -3) {     // the "keyboard" button toggles it
  g_vk_open = false;
  present();
  return;
 }
 int n = name_to_nav(name);
 int rows = (int)g_vk_rows.size();
 switch (n) {
  case NAV_UP:
   if (g_vk_r > 0) g_vk_r--;
   if (g_vk_c >= (int)g_vk_rows[g_vk_r].size()) g_vk_c = (int)g_vk_rows[g_vk_r].size() - 1;
   break;
  case NAV_DOWN:
   if (g_vk_r < rows - 1) g_vk_r++;
   if (g_vk_c >= (int)g_vk_rows[g_vk_r].size()) g_vk_c = (int)g_vk_rows[g_vk_r].size() - 1;
   break;
  case NAV_LEFT:  if (g_vk_c > 0) g_vk_c--; break;
  case NAV_RIGHT: if (g_vk_c < (int)g_vk_rows[g_vk_r].size() - 1) g_vk_c++; break;
  case NAV_OK:    g_keys.push_back(g_vk_rows[g_vk_r][g_vk_c].second); break;
  case NAV_DEL:   g_keys.push_back(127); break;
  case NAV_CANCEL: g_vk_open = false; break;
  default: break;
 }
 present();
}

static std::string full_name(const std::string &name)
{
 static const int mod_ids[4] = { 15, 16, 9, 10 };
 for (int i = 0; i < 4; i++)
  if (g_btn[mod_ids[i]] && name != PAD_NAMES[mod_ids[i]])
   return std::string(PAD_NAMES[mod_ids[i]]) + "+" + name;
 return name;
}

static void fire(const std::string &name)
{
 if (g_ui_active) {
  ui_in in;
  in.name = name;
  in.full = full_name(name);
  g_ui_q.push_back(in);
  g_ui_mod_pending = -1;
  return;
 }
 if (g_vk_open) {
  vk_input(name);
  return;
 }
 int key;
 if (lookup_binding(name, key)) {
  if (key == -3) {                 // open the on-screen keyboard
   if (g_vk_rows.empty()) vk_build();
   g_vk_open = true;
   present();
  } else if (key == -4) {          // open the controller settings
   g_open_ui = true;
  } else if (key >= 0) {
   g_keys.push_back(key);
  }
 }
}

static void pad_button(int id, bool down)
{
 if (id < 0 || id > 16)
  return;
 bool is_mod = (id == 9 || id == 10 || id == 15 || id == 16);
 if (down) {
  if (g_btn[id]) return;
  g_btn[id] = true;
  // L3 + R3 (both stick clicks) opens the in-game controller settings
  if ((id == 7 || id == 8) && g_btn[7] && g_btn[8] && !g_ui_active) {
   g_open_ui = true;
   return;
  }
  if (g_ui_active && is_mod) {
   g_ui_mod_pending = id;      // decided on release: combo prefix or plain button
   return;
  }
  fire(PAD_NAMES[id]);
  if (id >= 11 && id <= 14) {
   g_rep_dpad.active = true;
   g_rep_dpad.name = PAD_NAMES[id];
   g_rep_dpad.next = SDL_GetTicks() + g_rep_delay;
  }
 } else {
  g_btn[id] = false;
  if (g_ui_active && is_mod && g_ui_mod_pending == id) {
   ui_in in;
   in.name = PAD_NAMES[id];
   in.full = in.name;
   g_ui_q.push_back(in);
   g_ui_mod_pending = -1;
  }
  if (id >= 11 && id <= 14 && g_rep_dpad.active && g_rep_dpad.name == PAD_NAMES[id])
   g_rep_dpad.active = false;
 }
}

static void update_stick(int s)
{
 float x = g_ax[s * 2] / 32767.0f;
 float y = g_ax[s * 2 + 1] / 32767.0f;
 int dir = -1;
 if (x * x + y * y > g_deadzone * g_deadzone) {
  float ang = (float)atan2(-y, x);
  dir = ((int)floor(ang / (3.14159265f / 4.0f) + 0.5f) + 8) % 8;
 }
 if (dir == g_stick_dir[s])
  return;
 g_stick_dir[s] = dir;
 if (dir < 0) {
  g_rep_stick[s].active = false;
  return;
 }
 std::string name = std::string(s ? "RSTICK_" : "LSTICK_") + DIR_NAMES[dir];
 fire(name);
 g_rep_stick[s].active = true;
 g_rep_stick[s].name = name;
 g_rep_stick[s].next = SDL_GetTicks() + g_rep_delay;
}

static void pad_axis(int axis, int value)
{
 if (axis >= 0 && axis <= 3) {
  g_ax[axis] = value;
  update_stick(axis / 2);
 } else if (axis == 4 || axis == 5) {
  int id = (axis == 4) ? 15 : 16;
  if (value > 16000)      pad_button(id, true);
  else if (value < 8000)  pad_button(id, false);
 }
}

static bool repeating()
{
 return g_rep_dpad.active || g_rep_stick[0].active || g_rep_stick[1].active;
}

static void update_repeats()
{
 Uint32 now = SDL_GetTicks();
 repeater_t *reps[3] = { &g_rep_dpad, &g_rep_stick[0], &g_rep_stick[1] };
 for (int i = 0; i < 3; i++) {
  if (reps[i]->active && now >= reps[i]->next) {
   fire(reps[i]->name);
   reps[i]->next = now + g_rep_rate;
  }
 }
}


static void handle_event(const SDL_Event &e);

// ---------------------------------------------------------------------------
// In-game controller settings (open with L3+R3, or F6 on the keyboard).
// Draws straight into the screen buffer, so it works without the game's UI.
// ---------------------------------------------------------------------------
static std::string key_name(int key)
{
 if (key == -1)        return "none";
 if (key == -3)        return "keyboard";
 if (key == -4)        return "settings";
 if (key == 10)        return "enter";
 if (key == 27)        return "esc";
 if (key == 9)         return "tab";
 if (key == ' ')       return "space";
 if (key == 127)       return "backspace";
 if (key == KEY_UP)    return "up";
 if (key == KEY_DOWN)  return "down";
 if (key == KEY_LEFT)  return "left";
 if (key == KEY_RIGHT) return "right";
 char buf[2] = { (char)key, 0 };
 return std::string(buf);
}

static void save_controls()
{
 FILE *f = fopen("controls.cfg", "wb");
 if (!f)
  return;
 fprintf(f, "# Controller configuration (saved from the in-game menu: L3+R3 or F6).\n");
 fprintf(f, "# Values: a single character or enter esc tab space backspace up down left right none\n\n");
 fprintf(f, "repeat_delay = %d\nrepeat_rate = %d\ndeadzone = %.2f\n\n", g_rep_delay, g_rep_rate, g_deadzone);
 for (std::map<std::string, int>::iterator it = g_bind.begin(); it != g_bind.end(); ++it)
  fprintf(f, "%s = %s\n", it->first.c_str(), key_name(it->second).c_str());
 fclose(f);
}

static void scr_clear()
{
 for (int i = 0; i < SCR_ROWS * SCR_COLS; i++)
  blank_cell(g_screen[i]);
}

static void scr_text(int row, int col, const std::string &s, int fg, int bg)
{
 for (size_t i = 0; i < s.size(); i++) {
  int c = col + (int)i;
  if (row < 0 || row >= SCR_ROWS || c < 0 || c >= SCR_COLS)
   continue;
  cursecell &cc = g_screen[row * SCR_COLS + c];
  cc.ch = (unsigned char)s[i];
  cc.fg = (unsigned char)fg;
  cc.bg = (unsigned char)bg;
 }
}

// Waits for the next navigation input. If cap != NULL, returns NAV_CAPTURED with
// the raw input (e.g. "LT+A") instead. timeout_ms <= 0 waits forever.
static int ui_next(ui_in *cap, int *textkey, int timeout_ms)
{
 SDL_Event e;
 Uint32 start = SDL_GetTicks();
 for (;;) {
  update_repeats();
  while (!g_ui_q.empty()) {
   ui_in in = g_ui_q.front();
   g_ui_q.pop_front();
   if (cap) {
    *cap = in;
    return NAV_CAPTURED;
   }
   int n = name_to_nav(in.name);
   if (n != NAV_NONE)
    return n;
  }
  if (timeout_ms > 0 && SDL_GetTicks() - start >= (Uint32)timeout_ms)
   return NAV_CANCEL;
  if (!SDL_WaitEventTimeout(&e, 16))
   continue;
  if (e.type == SDL_KEYDOWN) {
   switch (e.key.keysym.sym) {
    case SDLK_UP:        return NAV_UP;
    case SDLK_DOWN:      return NAV_DOWN;
    case SDLK_LEFT:      return NAV_LEFT;
    case SDLK_RIGHT:     return NAV_RIGHT;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:  return NAV_OK;
    case SDLK_ESCAPE:    return NAV_CANCEL;
    case SDLK_DELETE:
    case SDLK_BACKSPACE: return NAV_DEL;
    default: break;
   }
   continue;
  }
  if (e.type == SDL_TEXTINPUT) {
   unsigned char ch = (unsigned char)e.text.text[0];
   if (!cap && textkey && ch >= 33 && ch < 127) {
    *textkey = ch;
    return NAV_TEXT;
   }
   continue;
  }
  handle_event(e);
 }
}

typedef std::pair<std::string, int> keyitem;

// Returns the chosen key, or -2 if cancelled.
static int ui_pick_key(const std::string &title, int current)
{
 std::vector<std::vector<keyitem> > rows;
 std::vector<keyitem> r;
 r.push_back(keyitem("enter", 10));
 r.push_back(keyitem("esc", 27));
 r.push_back(keyitem("tab", 9));
 r.push_back(keyitem("space", ' '));
 r.push_back(keyitem("backspace", 127));
 r.push_back(keyitem("none", -1));
 r.push_back(keyitem("keyboard", -3));
 rows.push_back(r);
 r.clear();
 r.push_back(keyitem("up", KEY_UP));
 r.push_back(keyitem("down", KEY_DOWN));
 r.push_back(keyitem("left", KEY_LEFT));
 r.push_back(keyitem("right", KEY_RIGHT));
 r.push_back(keyitem("settings", -4));
 rows.push_back(r);
 r.clear();
 for (int ch = 33; ch < 127; ch++) {
  char b[2] = { (char)ch, 0 };
  r.push_back(keyitem(std::string(b), ch));
  if (r.size() == 16) { rows.push_back(r); r.clear(); }
 }
 if (!r.empty())
  rows.push_back(r);

 int cr = 0, cc = 0;
 for (size_t i = 0; i < rows.size(); i++)
  for (size_t j = 0; j < rows[i].size(); j++)
   if (rows[i][j].second == current) { cr = (int)i; cc = (int)j; }

 for (;;) {
  scr_clear();
  scr_text(0, 2, "Bind " + title + "   (now: " + (current == -2 ? "unbound" : key_name(current)) + ")", 11, 0);
  scr_text(1, 2, "Choose the key this input should press.", 8, 0);
  for (size_t i = 0; i < rows.size(); i++) {
   int width = (i < 2) ? 11 : 4;
   int srow = 3 + (int)i + (i >= 2 ? 1 : 0);
   for (size_t j = 0; j < rows[i].size(); j++) {
    bool sel = ((int)i == cr && (int)j == cc);
    std::string label = rows[i][j].first;
    if (sel) scr_text(srow, 2 + (int)j * width, " " + label + " ", 0, 7);
    else     scr_text(srow, 2 + (int)j * width, " " + label, 7, 0);
   }
  }
  scr_text(23, 2, "D-pad/stick: move   A: choose   B: cancel   (keyboard: just type the key)", 8, 0);
  present();

  int tk = 0;
  int n = ui_next(NULL, &tk, 0);
  switch (n) {
   case NAV_UP:    if (cr > 0) cr--; if (cc >= (int)rows[cr].size()) cc = (int)rows[cr].size() - 1; break;
   case NAV_DOWN:  if (cr < (int)rows.size() - 1) cr++; if (cc >= (int)rows[cr].size()) cc = (int)rows[cr].size() - 1; break;
   case NAV_LEFT:  if (cc > 0) cc--; break;
   case NAV_RIGHT: if (cc < (int)rows[cr].size() - 1) cc++; break;
   case NAV_OK:    return rows[cr][cc].second;
   case NAV_CANCEL: return -2;
   case NAV_TEXT:  return tk;
   default: break;
  }
 }
}

static void ui_capture_and_bind()
{
 scr_clear();
 scr_text(0, 2, "REBIND AN INPUT", 11, 0);
 scr_text(3, 2, "Press the button (or push the stick) you want to change.", 7, 0);
 scr_text(5, 2, "For a combo, HOLD LT / RT / LB / RB first, then press the other input.", 7, 0);
 scr_text(7, 2, "To rebind a modifier itself, press and release it alone.", 7, 0);
 scr_text(23, 2, "Keyboard Esc or waiting 15 seconds cancels.", 8, 0);
 present();
 ui_in cap;
 int r;
 do {
  r = ui_next(&cap, NULL, 15000);
 } while (r != NAV_CAPTURED && r != NAV_CANCEL);
 if (r != NAV_CAPTURED)
  return;
 std::map<std::string, int>::iterator it = g_bind.find(cap.full);
 int cur = (it == g_bind.end()) ? -2 : it->second;
 int k = ui_pick_key(cap.full, cur);
 if (k != -2)
  g_bind[cap.full] = k;
}

static void ui_edit_list()
{
 int cur = 0, top = 0;
 for (;;) {
  std::vector<std::string> names;
  for (std::map<std::string, int>::iterator it = g_bind.begin(); it != g_bind.end(); ++it)
   names.push_back(it->first);
  if (cur >= (int)names.size()) cur = (int)names.size() - 1;
  if (cur < 0) cur = 0;
  if (cur < top) top = cur;
  if (cur >= top + 18) top = cur - 17;

  scr_clear();
  scr_text(0, 2, "BINDINGS LIST", 11, 0);
  if (names.empty())
   scr_text(3, 2, "(no bindings)", 8, 0);
  for (int i = 0; i < 18 && top + i < (int)names.size(); i++) {
   const std::string &nm = names[top + i];
   std::string line = " " + nm;
   while (line.size() < 24) line += ' ';
   line += " -> " + key_name(g_bind[nm]) + " ";
   bool sel = (top + i == cur);
   scr_text(3 + i, 2, line, sel ? 0 : 7, sel ? 7 : 0);
  }
  scr_text(23, 2, "A: change key   X: delete   B: back", 8, 0);
  present();

  int n = ui_next(NULL, NULL, 0);
  switch (n) {
   case NAV_UP:   if (cur > 0) cur--; break;
   case NAV_DOWN: if (cur < (int)names.size() - 1) cur++; break;
   case NAV_OK:
    if (!names.empty()) {
     int k = ui_pick_key(names[cur], g_bind[names[cur]]);
     if (k != -2) g_bind[names[cur]] = k;
    }
    break;
   case NAV_DEL:
    if (!names.empty()) g_bind.erase(names[cur]);
    break;
   case NAV_CANCEL: return;
   default: break;
  }
 }
}

static void ui_main()
{
 int cur = 0;
 for (;;) {
  char buf[96];
  std::string items[7];
  items[0] = "Rebind an input (press the button you want to change)";
  items[1] = "Edit bindings list";
  snprintf(buf, sizeof(buf), "Repeat delay: %d ms   (left/right to change)", g_rep_delay);
  items[2] = buf;
  snprintf(buf, sizeof(buf), "Repeat rate: %d ms   (left/right to change)", g_rep_rate);
  items[3] = buf;
  snprintf(buf, sizeof(buf), "Stick deadzone: %.2f   (left/right to change)", g_deadzone);
  items[4] = buf;
  items[5] = "Reset everything to defaults";
  items[6] = "Close and save";

  scr_clear();
  scr_text(0, 2, "CONTROLLER SETTINGS", 11, 0);
  for (int i = 0; i < 7; i++) {
   bool sel = (i == cur);
   scr_text(3 + i * 2, 2, " " + items[i] + " ", sel ? 0 : 7, sel ? 7 : 0);
  }
  scr_text(23, 2, "D-pad/stick: move   A: select   B: close and save", 8, 0);
  present();

  int n = ui_next(NULL, NULL, 0);
  switch (n) {
   case NAV_UP:   if (cur > 0) cur--; break;
   case NAV_DOWN: if (cur < 6) cur++; break;
   case NAV_LEFT:
   case NAV_RIGHT: {
    int d = (n == NAV_LEFT) ? -1 : 1;
    if (cur == 2) { g_rep_delay += d * 50; if (g_rep_delay < 0) g_rep_delay = 0; if (g_rep_delay > 1000) g_rep_delay = 1000; }
    if (cur == 3) { g_rep_rate += d * 10; if (g_rep_rate < 16) g_rep_rate = 16; if (g_rep_rate > 500) g_rep_rate = 500; }
    if (cur == 4) { g_deadzone += d * 0.05f; if (g_deadzone < 0.1f) g_deadzone = 0.1f; if (g_deadzone > 0.95f) g_deadzone = 0.95f; }
    break;
   }
   case NAV_OK:
    if (cur == 0) ui_capture_and_bind();
    else if (cur == 1) ui_edit_list();
    else if (cur == 5) {
     g_bind.clear();
     g_rep_delay = 300; g_rep_rate = 120; g_deadzone = 0.5f;
     parse_cfg(DEFAULT_CFG);
    }
    else if (cur == 6) return;
    break;
   case NAV_CANCEL: return;
   default: break;
  }
 }
}

static void run_config_ui()
{
 static cursecell backup[SCR_ROWS * SCR_COLS];
 memcpy(backup, g_screen, sizeof(backup));
 g_ui_active = true;
 g_vk_open = false;
 g_toast_until = 0;
 g_ui_q.clear();
 g_ui_mod_pending = -1;
 g_rep_dpad.active = false;
 g_rep_stick[0].active = false;
 g_rep_stick[1].active = false;

 ui_main();
 save_controls();

 g_ui_active = false;
 g_ui_q.clear();
 memcpy(g_screen, backup, sizeof(backup));
 for (int i = 0; i < 17; i++) g_btn[i] = false;
 g_rep_dpad.active = false;
 g_rep_stick[0].active = false;
 g_rep_stick[1].active = false;
 g_stick_dir[0] = g_stick_dir[1] = -1;
 g_keys.clear();
 present();
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
  case SDL_CONTROLLERDEVICEADDED:
   SDL_GameControllerOpen(e.cdevice.which);
   show_toast("Controller connected - L3+R3: settings, START: on-screen keyboard", 6000);
   break;
  case SDL_CONTROLLERDEVICEREMOVED: {
   SDL_GameController *c = SDL_GameControllerFromInstanceID(e.cdevice.which);
   if (c) SDL_GameControllerClose(c);
   for (int i = 0; i < 17; i++) g_btn[i] = false;
   g_rep_dpad.active = false;
   g_rep_stick[0].active = g_rep_stick[1].active = false;
   g_stick_dir[0] = g_stick_dir[1] = -1;
   break;
  }
  case SDL_CONTROLLERBUTTONDOWN:
   pad_button(e.cbutton.button, true);
   break;
  case SDL_CONTROLLERBUTTONUP:
   pad_button(e.cbutton.button, false);
   break;
  case SDL_CONTROLLERAXISMOTION:
   pad_axis(e.caxis.axis, e.caxis.value);
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
    case SDLK_F5:        load_controls(); break;
    case SDLK_F6:        g_open_ui = true; break;
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
 if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
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
 load_controls();
 show_toast(g_first_run ? "Controller: L3+R3 = settings, START = on-screen keyboard (F6 on a PC keyboard)"
                       : "L3+R3: controller settings   |   START: on-screen keyboard", g_first_run ? 15000 : 7000);

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
  if (g_open_ui) {
   g_open_ui = false;
   run_config_ui();
   start = SDL_GetTicks();
  }
  if (g_toast_until && SDL_GetTicks() >= g_toast_until) {
   g_toast_until = 0;
   present();
  }
  update_repeats();
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
   if (repeating() || g_toast_until) {
    if (SDL_WaitEventTimeout(&e, repeating() ? 16 : 100))
     handle_event(e);
   } else if (SDL_WaitEvent(&e)) {
    handle_event(e);
   }
   continue;
  }
  Uint32 elapsed = SDL_GetTicks() - start;
  if (elapsed >= (Uint32)g_timeout)
   return ERR;
  int wait = (int)((Uint32)g_timeout - elapsed);
  if (repeating() && wait > 16)
   wait = 16;
  if (SDL_WaitEventTimeout(&e, wait))
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
