#!/usr/bin/env python3
# Adds (or updates) a "Controller (gamepad) settings" page in the in-game help (help.cpp).
# Run inside the Cataclysm folder:   python3 patch_help.py
import sys

path = "help.cpp"
OLD_LABEL = 'o: Controller (gamepad) settings\\n\\\n'
NEW_LABEL = 'o: Controller settings  (gamepad: START opens keyboard, pick o)\\n\\\n'
s = open(path).read()

page = '''  case 'o':
  case 'O':
   erase();
   mvprintz(0, 0, c_white, "\\
CONTROLLER (GAMEPAD) SETTINGS\\n\\
\\n\\
This version supports game controllers (Xbox, Switch Pro, and similar).\\n\\
\\n\\
L3 + R3 (press both sticks)  opens the controller settings menu.\\n\\
START                        opens an on-screen keyboard, so you can type\\n\\
                             any letter, number or symbol with the controller.\\n\\
(On a PC keyboard: F6 = settings.)\\n\\
\\n\\
In the settings menu you can:\\n\\
 - Rebind any button, stick direction or combination to any key\\n\\
 - Hold LT, RT, LB or RB while pressing another input to make combos\\n\\
   (for example LT + A)\\n\\
 - Change the stick deadzone and the key repeat speed\\n\\
\\n\\
Your changes are saved automatically.\\n\\
\\n\\
Press any key to return.");
   getch();
   break;
'''

start_marker = "  case 'o':\n  case 'O':\n"
end_marker = "  case 'a':\n  case 'A':\n"

if start_marker in s:
    # already patched: replace the old page with the current text
    a = s.index(start_marker)
    b = s.index(end_marker)
    s = s[:a] + page + s[b:]
    s = s.replace(OLD_LABEL, NEW_LABEL)
    open(path, "w").write(s)
    print("Done: controller help page updated. Now run: make")
    sys.exit(0)

menu_old = 'Press q or ESC to return to the game.\\n\\\n\\n\\\na: Introduction\\n\\\n'
menu_new = 'Press q or ESC to return to the game.\\n\\\na: Introduction\\n\\\n'
if menu_old not in s:
    print("Could not find the help menu header - not changed.")
    sys.exit(1)
s = s.replace(menu_old, menu_new, 1)

n_old = 'n: Survival tips\\n\\\n'
n_new = 'n: Survival tips\\n\\\n' + NEW_LABEL
if n_old not in s:
    print("Could not find the 'n: Survival tips' line - not changed.")
    sys.exit(1)
s = s.replace(n_old, n_new, 1)

if end_marker not in s:
    print("Could not find the first 'case' of the help switch - not changed.")
    sys.exit(1)
s = s.replace(end_marker, page + end_marker, 1)

open(path, "w").write(s)
print("Done: help.cpp patched. Now run: make")
