![](dk.png)

A list based tiling window manager in the vein of dwm, bspwm, and xmonad.

Some basics:

- Heavily scriptable and tinker friendly.
- Dynamic workspaces, any workspace on any monitor.
- More dynamic tile layout with multiple stacks and window resizing.
- Gaps, double borders, additional layouts, padding and more.
- Better support for mouse and floating windows, resize tiles with mouse.
- Startup script for configuration and running programs.
- Status info can be output to a file or piped for use in bars or scripts.
- No built-in extras *(bar, font drawing, or key bindings)*.
- Sane support for
[icccm](https://www.x.org/releases/X11R7.6/doc/xorg-docs/specs/ICCCM/icccm.html#client_to_window_manager_communication),
[ewmh](https://specifications.freedesktop.org/wm-spec/wm-spec-latest.html), and
[motif](http://www.ist.co.uk/motif/books/vol6A/ch-20.fm.html#963509).


### Installation

You need the xcb headers

Arch
```
xcb-proto xcb-util xcb-util-wm xcb-util-cursor xcb-util-keysyms
```

Void
```
libxcb-devel xcb-proto xcb-util-devel xcb-util-wm-devel xcb-util-cursor-devel xcb-util-keysyms-devel
```

Debian/Ubuntu
```
libxcb-randr0-dev libxcb-util-dev libxcb-icccm4-dev libxcb-cursor-dev libxcb-keysyms1-dev
```

Other systems should have packages with similar names.

As mentioned above dk has no keybind support so you'll need a separate  
program like `sxhkd` to launch programs and control the window manager.


To compile run
```
make
```

Edit `config.h` if needed, then run *(as root if needed)*
```
make install
```

If at any time you want to uninstall, run
```
make uninstall
```


### Usage

To start dk you can add the following to your `~/.xinitrc`
```
exec dk
```

Optionally copy the example dkrc and/or sxhkdrc to `~/.config/dk/`
```
mkdir -p ~/.config/dk
cp /usr/local/share/doc/dk/dkrc ~/.config/dk/
cp /usr/local/share/doc/dk/sxhkdrc ~/.config/dk/
```

### Configuration

There are example `dkrc` and `sxhkdrc` files in `doc/` or  
`/usr/local/share/doc/dk` after installation.

dk looks for an rc file in the following order
```
$DKRC                     # user specified location
$HOME/.config/dk/dkrc     # default location
```
and tries to run it, **it must be executable in order for this to happen**.

Advanced changes and configuration like new layouts, callbacks, or commands  
can be done by copying the default config header `config.def.h` to `config.h`,  
editing it and recompiling. This file isn't tracked by git so you can keep your  
configuration and avoid conflicts when pulling new updates.

### dkcmd
Most of your interaction with the window manager will be using `dkcmd`  
which writes one or more commands into the socket where it is then read  
and parsed by the window manager *(see Commands section below)*.


### Syntax Outline
The commands have a very basic syntax and parsing, the input is broken  
down into smaller pieces *(tokens)* which are then passed to the matching  
keyword function, otherwise an error is returned.

Tokens are delimited by one or more:

- whitespace *(space or tab)*

- quotation mark *(`'` or `"`)*

- equal sign *(`=`)*

This means the following inputs are all equivalent.
```
setting=value
setting value
setting="value"
setting = 'value'
setting "value"
setting		"value"
```
and result in two tokens: `setting` and `value`

---

Quotation exists as a way to preserve whitespace and avoid interpretation by the shell,  
otherwise we have no way of determining whether an argument is a continuation of the  
previous or the beginning of the next. Consider the following
```
title="^open files$"
```

If the value being matched has quotes in it, they can be escaped or strong quoted
```
title="^\"preserved quotes\"$"
title='^"preserved quotes"$'
```

---

For various commands dk will expect a certain data type or format to be given.

- string: normal plain text, must be less than 256 characters.

- boolean: `true`, `false`, `1`, or `0`.

- hex: `(0x/#)XXXXXXXX`, used for window ids

- integer: `(+/-)1`, if it is preceded by a sign it is considered relative.

- float: `(+/-)0.1`, same as integer but must contain a decimal value.

- colour: `(0x/#)[AA]RRGGBB`, hex value, if no alpha channel is given the colour is opaque.

---

### Commands
```
dkcmd COMMAND
```
#### WM

- `quit` exit dk.
- `restart` re-execute dk.

#### Ws and Mon
`mon` and `ws` operate on monitors and workspaces respectively.

- `CLIENT` (hex) The window id in hex to operate on, if unspecified the active window is used.
- `TARGET` (integer/string) Name or number of the workspace or monitor to target or strings
    - `next` relative forward
    - `prev` relative backward
    - `last` last viewed
    - `nextne` next non-empty
    - `prevne` previous non-empty

```
ws  [SUBCOMMAND] [CLIENT] TARGET
mon [SUBCOMMAND] [CLIENT] TARGET
```

###### Subcommands
`view` View the TARGET, default if no subcommand is given.
```
ws view TARGET
ws TARGET
```
---

`send` Send CLIENT to the TARGET.
```
mon send [CLIENT] TARGET
```
---

`follow` Follow CLIENT to the TARGET.
```
ws follow [CLIENT] TARGET
```

#### Rule
`rule` operates on window rules.

- `MATCH` one or more regex strings to be used when matching window properties.
- `SETTING` one or more window setting to be applied when a matched window is encountered.

```
rule [SUBCOMMAND] MATCH SETTING
```

###### Subcommands

`apply` applies RULE to all matching windows, if RULE is `*` apply all rules and MATCH is ignored.
```
rule apply RULE [MATCH]
```
---

`remove` removes RULE, if RULE is `*` remove all rules and MATCH is ignored.

```
rule remove RULE [MATCH]
```

###### Settings

`class` `instance` `title` `type` (string) regex to match the window class, instance, title, and  
type respectively *(may be prefixed with match_ for clarity)*. Regex matching is always done **case insensitive**  
with extended regex mode enabled.
```
rule [SUBCOMMAND] class="^firefox$" instance="^navigator$" title="^mozilla firefox$" type=dialog [SETTING]
```


`type` currently only supports `dialog` and `splash` windows, all others are treated as normal windows.

---

`ws` (integer/string) determine what workspace the window should be on.
```
rule MATCH ws=1      # using index
rule MATCH ws=term   # using name
```
---

`mon` (integer/string) determine what monitor the window should be on.
```
rule MATCH mon=1          # using index
rule MATCH mon=HDMI-A-0   # using name
```
---

`x` `y` `w` `width` `h` `height` `bw` `border_width` (integer/string) determine the window location and size.

- `x` change the x coordinate, can be an integer or one of the following.
    - `center left` and `right` gravitate on the x coordinate.

- `y` change the y coordinate, can be an integer or one of the following.
    - `center top` and `bottom` gravitate on the y coordinate.

- `w` `width` change the window width.
- `h` `height` change the window height.
- `bw` `border_width` change the window border width.

```
rule MATCH x=20 y=100 w=1280 h=720 bw=0         # using absolute values
rule MATCH x=center y=center w=1280 h=720 bw=0  # using gravities
```
---

`callback` (string) determine a callback function to be invoked on window open and close.
These are defined in the config header and compiled into the source, one example is provided.
```
rule MATCH callback=albumart
```
---

`float` `stick` (boolean) determine if the window should be floating or stick respectively.
```
rule MATCH float=true stick=true
```
---

`ignore_cfg` (boolean) determine if the window should ignore configure request  
events (size or location changes).
```
rule MATCH ignore_cfg=true
```
---

`ignore_msg` (boolean) determine if the window should ignore client message  
window activation events (grabbing focus).
```
rule MATCH ignore_msg=true
```
---

`focus` (boolean) determine if the window should be focused and `view` it's workspace.  
If `mon` is also set it will be activated first before viewing the workspace.
```
rule MATCH focus=true
```
---

#### Set
`set` operates on workspace or global configuration settings.

- `SETTING` one or more settings to be changed.
- `WS` the workspace which subcommand should apply to, if unspecified the current is used.  
`_` is a special workspace used to define default values for new workspaces which  
haven't been created yet.

```
set [WS] SETTING
set ws=_ [apply] SETTING
```

###### Settings
`numws` (integer) change the number of workspaces to allocate.
```
set numws=10
```
---

`name` (string) change the WS name.
```
set ws=1 name="term"
```
---

`static_ws` (boolean) disable dynamic workspaces for multi-head systems.
```
set static_ws=false
```
---

`mon` (integer/string) change which monitor WS should be on (requires `static_ws=true`).
```
set ws=1 mon=HDMI-A-0
set ws=1 mon=1
```
---

`master` `stack` (integer) change the number of windows to occupy the master area (tile layout).
```
set [WS] stack  3            # absolute values have no signs
set [WS] master +1 stack -1  # relative change with signed integers (+/-)
```
---

`msplit` `ssplit` (float) change the workspace master or stack split ratios respectively.
```
set [WS] msplit +0.1
set [WS] ssplit 0.55
```
---

`gap` (integer) change the workspace gap width.
```
set [WS] gap 10
```
---

`tile_hints` (boolean) obey size hints in tiled layouts.
```
set tile_hints=true
```
---

`tile_tohead` (boolean) place new windows at the head of the list in tiled layouts.
```
set tile_tohead=true
```
---

`smart_gap` (boolean) whether gaps are disabled on workspaces with only one tiled window.
```
set smart_gap=true
```
---

`smart_border` (boolean) whether borders are disabled on workspaces with only one tiled window.
```
set smart_border=true
```
---

`focus_urgent` (boolean) focus windows that request it.
```
set focus_urgent=true
```
---

`focus_open` (boolean) whether windows are focused when opened.
```
set focus_open=false
```
---

`focus_mouse` (boolean) whether window focus follows the mouse.
```
set focus_mouse=false
```
---

`obey_motif` (boolean) whether to obey motif hints for borders.
```
set obey_motif=false
```
---

`win_minxy` (integer) amount of window (in pixels) to be kept on the screen when moving.
```
set win_minxy=20
```
---

`win_minwh` (integer) minimum window size.
```
set win_minwh=50
```
---

`apply` when changing the default `_` workspace apply settings to existing real workspaces.
```
set ws=_ apply SETTING
```
---

`layout` (string) change the workspace window layout.

- `tile` default tile layout
- `rtile` tile layout with master area on the right
- `mono` windows arranged maximized and stacked
- `grid` all windows try to occupy equal space
- `spiral` windows shrink by 1/2 towards the center of the screen
- `dwindle` windows shrink by 1/2 towards the bottom right of the screen
- `none` floating, no layout

```
set [WS] layout mono
```
---

`border` change the window border sizes and colours.

- `w` `width` (integer) change the overall window border width.
- `ow` `outer` `outer_width` (integer) change the outer border width (greater than 0 makes double borders).
- `colour` `color` (string) change the border (overall and outer) colour for various window states.
    - `f` `focus` (colour) the active window border overall colour.
    - `r` `urgent` (colour) urgent window border overall colour.
    - `u` `unfocus` (colour) normal window border overall colour.
    - `of` `outer_focus` (colour) the active window outer border colour.
    - `or` `outer_urgent` (colour) urgent window outer border colour.
    - `ou` `outer_unfocus` (colour) normal window outer border colour.

```
set border w=5 ow=3 colour f='#6699cc' u='#444444' r='#ee5555' of='#222222' ou='#222222' or='#222222'
```
---

`pad` change the workspace padding.

- `l` `left` (integer) change the workspace left side padding.
- `r` `right` (integer) change the workspace right side padding.
- `t` `top` (integer) change the workspace top padding.
- `b` `bottom` (integer) change the workspace bottom padding.

```
set [WS] pad l=50 r=50 t=50 b=50
```
---

`mouse` change the mouse binds for move and resize (global, does not take a workspace).

- `mod` (string) change the modifier used in combination with move resize buttons.
    - `alt` `mod1` Alt key (default).
    - `super` `mod4` Win key.
    - `ctrl` `control` Ctrl key.

- `move` `resize` (string) change the button used for move and resize respectively.
    - `button1` left mouse button.
    - `button2` right mouse button.
    - `button3` middle mouse button.

```
set mouse move=button1 resize=button2 mod=mod1
```
---

#### Win
`win` operates on windows.

- `CLIENT` (hex) the window id, if unspecified the current window is used.

```
win [CLIENT] ACTION
```

###### Settings
`cycle` cycle windows in place.
```
win cycle
```
---

`float` change the window floating state.
```
win [CLIENT] float
```
---

`full` change the window fullscreen state.
```
win [CLIENT] full
```
---

`fakefull` change the window fake fullscreen state (allow moving, resizing, and tiling when fullscreen).
```
win [CLIENT] fakefull
```
---

`stick` change the window sticky state.
```
win [CLIENT] stick
```
---

`swap` change the window between it's current location and master.
```
win [CLIENT] swap
```
---

`kill` close the window.
```
win [CLIENT] kill
```
---

`focus` (integer/string) change the focused window.

- `next` focus the next window.
- `prev` focus the previous window.

```
win CLIENT focus  # focus window by id
win focus next    # focus the next window
win focus +2      # focus two windows ahead
```
---

`mvstack` (integer/string) move a tiled window around the stack.

- `up` move the tiled window up the stack.
- `down` move the tiled window down the stack.

```
win [CLIENT] mvstack up
```
---

`resize` change the window size, location, and border width.

- `x` change the x coordinate, can be an integer or one of the following.
    - `center left` and `right` gravitate on the x coordinate.

- `y` change the y coordinate, can be an integer or one of the following.
    - `center top` and `bottom` gravitate on the y coordinate.

- `w` `width` change the window width.
- `h` `height` change the window height.
- `bw` `border_width` change the window border width.

```
win [CLIENT] resize x=100 y=100 w=1280 h=720 bw=1
win [CLIENT] resize x=center y=center w=1280 h=720 bw=1
```
---
#### Status
`status` print status information to a file or stdout.

```
status [TYPE] [FILE] [NUM]
```

###### Subcommands
`type` (string) the type of status info to output.

- `ws` output workspace info.
- `win` output current window title.
- `layout` output current workspace layout name.
- `bar` output simple info for use in bars *(win, layout, and ws combined)*.
- `full` output the full wm and managed client state.

```
status type=ws [FILE] [NUM]
```
---

`file` (string) the location of the status file.

```
status file=/tmp/dk.status [TYPE] [NUM]
```
---

`num` (integer) the number of times to output, -1 is infinite and default if not specified.

```
status [TYPE] [FILE]        # output forever
status num=1 [TYPE] [FILE]  # output once
```


### Todo

- Simplification and code quality improvement.
- See ISSSUE.md for current problems and possible additions.
- `grep -n 'TODO' src/*` will give some code I'm not happy with.


### Contributing

I'm very open to contributions or ideas, please feel free to email me or open 
an issue/PR. For myself I have various builds that can help finding issues or 
provide some insight. These include:


stderr debug output
```
make debug
```

debug output and function calls
```
make fdebug
```

don't strip debug symbols *(for gdb, valgrind, etc.)*.
```
make nostrip
```


### Credits

See the LICENSE file for a list of authors/contributors.

Huge thanks to Kjetil Molteberg *(@badkarma)* for the logo.

Non contributors that I owe a huge thanks to:
- [dwm](https://dmw.suckless.org)
- [bspwm](https://github.com/baskerville/bspwm)
- [xmonad](https://xmonad.org)
- [evilwm](http://www.6809.org.uk/evilwm/)
- [monsterwm-xcb](https://github.com/Cloudef/monsterwm-xcb)
- [frankenwm](https://github.com/sulami/FrankenWM).

