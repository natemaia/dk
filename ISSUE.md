# TODOs and issues for dk

1. When entering full screen mode and open other windows, dk does not automatically
   exit full screen mode and this causes various glitches. I believe that if the
   application is in full screen mode, then when opening windows of any kind (tiled,
   floating, pop-up), the application should automatically exit the full screen mode.

2. In tile layout we can only change the width of the master window, and the stack
   windows can only be changed in height.

3. There is a free game war thunder in steam which for some reason does not go into
   full screen mode automatically. It's not hard for me to press the keyboard
   shortcut to go to full screen, but I've tested it in many other window managers
   and it doesn't have this problem. I think that the problem does not apply to a
   single game and can be deeper and appear in a different use case.

4. In the gparted program, when confirming operations, nothing is visible in the
   pop-up window that appears showing the progress of operations; there is also a
   problem with the border (I will attach a screenshot).

5. Regarding the fact that the dk does not exit full-screen mode when opening other
   windows. Strictly speaking, this is not a bug, but the problem is that when a new
   window opens, it receives focus but the window itself is not visible, so you have
   to return to the previous window and manually exit the fullscreen, or alternatively,
   exit the fullscreen in advance before opening another window :) Window managers
   such as i3, bspwm, hyprland implement automatic exit from full-screen mode when
   opening another window, it seems to me that this is the best solution.

6. I will also clarify that it is impossible to resize windows when the stack window
   is in focus: I use the tile layout with the following parameters
   ```dkcmd set ws=_ apply layout=tile master=1 stack=0 gap=6 msplit=0.55 ssplit=0.50```
   that is, in the stack has only one window vertically, like in vanilla dwm. So I
   can only resize when the master window has focus, but when the stack window has
   focus, "dkcmd win resize {w=-40,w=+40}" does nothing.
