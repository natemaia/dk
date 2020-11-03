## dk */dəˈkā/*

A list based tiling window manager in the vein of dwm, bspwm, and xmonad.

Some basics:

- Fully scriptable.
- Dynamic workspaces.
- More dynamic tile layout.
- Gaps, fancy borders, extra layouts and more.
- Better support for mouse and floating windows.
- Startup script for configuration and running programs.
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

Debian/Ubuntu
```
build-essential libxcb-randr0-dev libxcb-util-dev libxcb-icccm4-dev libxcb-cursor-dev libxcb-keysyms1-dev
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

Optionally copy the example dkrc and/or sxhkdrc to their respective locations
```
mkdir -p ~/.config/sxhkd ~/.config/dk
cp /usr/local/share/doc/dk/sxhkdrc ~/.config/sxhkd/
cp /usr/local/share/doc/dk/dkrc ~/.config/dk/
chmod +x ~/.config/dk/dkrc
```

### Configuration

There are example `dkrc` and `sxhkdrc` files in `doc/` or  
`/usr/local/share/doc/dk` after installation.

dk looks for a file in the following order
```
$DKRC                     # user specified location
$HOME/.config/dk/dkrc     # default location
```
and to runs it, **it must be executable in order for this to happen**.

Advanced changes and configuration like new layouts, callbacks, or new commands  
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

#### Ws and Mon
`mon` and `ws` operate on monitors and workspaces respectively.

- `CLIENT` (hex) The window id in hex to operate on, if unspecified the active window is used.
- `TARGET` (string/integer) Name or number of the workspace or monitor to target or  
relative strings `next`, `prev`, `last`, `nextne`, and `prevne`; `*ne` signify non-empty.

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

`send` Send CLIENT to the TARGET.
```
mon send [CLIENT] TARGET
```

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

`remove` removes RULE, if RULE is `*` remove all rules and MATCH is ignored.

```
rule remove RULE [MATCH]
```

###### Settings

`class` `instance` `title` (string) regex to match the window class, instance, and  
title respectively. Regex matching is always done **case insensitive** with extended mode enabled.
```
rule [SUBCOMMAND] class="^firefox$" instance="^navigator$" title="^mozilla firefox$" [SETTING]
```

`ws` (string/integer) determine what workspace the window should be on.
```
rule MATCH ws=1      # using index
rule MATCH ws=term   # using name
```

`mon` (string/integer) determine what monitor the window should be on.
```
rule MATCH mon=1          # using index
rule MATCH mon=HDMI-A-0   # using name
```

`x` `y` (integer/string) determine the window location using absolute values or gravities.
```
rule MATCH x=20 y=100                                # using absolute values
rule MATCH x=center/left/right y=center/top/bottom   # using gravities
```

`w/width` `h/height` `bw/border_width` (integer) determine the window location, size,
and border width respectively.
```
rule MATCH w=1280 h=720 bw=0
```

`callback` (string) determine a callback function to be invoked on window open and close.
```
rule MATCH callback=albumart
```

`float` `stick` (boolean) determine if the window should be floating or stick respectively.
```
rule MATCH float=true stick=true
```

`focus` (boolean) determine if the window should be focused and `view` it's workspace.  
If `mon` is also set it will be activated first before viewing the workspace.
```
rule MATCH focus=true
```

---

#### Set
`set` operates on workspace or global configuration settings.

- `SETTING` one or more subcommand settings to be changed.
- `WS` the workspace which subcommand should apply to, if unspecified the current is used.  
`default` is a special workspace used to define default values for new workspaces which  
haven't been created yet.

```
set [WS] SUBCOMMAND SETTING
```

###### Subcommands
`master` `stack` (integer) change the number of windows to occupy the master area (tile layout).
```
set [WS] master VALUE
set [WS] stack  VALUE
```

`msplit` `ssplit` (integer) change the workspace master or stack split ratios respectively.
```
set [WS] msplit VALUE
set [WS] ssplit VALUE
```

`gap` (integer) change the workspace gap width.
```
set [WS] gap VALUE
```

`layout` (string) change the workspace window layout.
```
set [WS] layout VALUE
```
- `tile` default tile layout
- `mono` windows arranged maximized and stacked
- `grid` all windows try to occupy equal space
- `spiral` windows shrink by 1/2 towards the center of the screen
- `dwindle` windows shrink by 1/2 towards the bottom right of the screen
- `none` floating, no layout


`border` change the window border sizes and colours.
```
set [WS] border VALUE
```
- `w` `width` (integer) change the overall window border width.
- `ow` `outer` `outer_width` (integer) change the outer border width (greater than 0 makes double borders).
- `colour` `color` (string) change the border (overall and outer) colour for various window states.
  - `focus` (colour) the active window border overall colour.
  - `urgent` (colour) urgent window border overall colour.
  - `unfocus` (colour) normal window border overall colour.
  - `outer_focus` (colour) the active window outer border colour.
  - `outer_urgent` (colour) urgent window outer border colour.
  - `outer_unfocus` (colour) normal window outer border colour.


`pad` change the workspace padding.
```
set [WS] pad VALUE
```
- `l` `left` (integer) change the workspace left side padding.
- `r` `right` (integer) change the workspace right side padding.
- `t` `top` (integer) change the workspace top padding.
- `b` `bottom` (integer) change the workspace bottom padding.


`mouse` change the mouse binds for move and resize (global, does not take a workspace).
```
set mouse VALUE
```
- `mod` (string) change the modifier used in combination with move resize buttons.
  - `alt` `mod1` Alt key (default).
  - `super` `mod4` Win key.
  - `ctrl` `control` Ctrl key.
- `move` `resize` (string) change the button used for move and resize respectively.
  - `button1` left mouse button.
  - `button2` right mouse button.
  - `button3` middle mouse button.


---

#### Win
`win` operates on windows.
```
win [SUBCOMMAND] [CLIENT] ACTION
```
---

### Todo

- Simplification.


### Contributing

I'm very open to contributions or ideas.


To enable internal stderr debug output
```
make debug
```

To leave debug symbols in *(for gdb, valgrind, etc.)*.
```
make nostrip
```


### Credits

See the LICENSE file for a list of authors/contributors.

Non contributors that I owe a huge thanks to:
[dwm](https://dmw.suckless.org), [bspwm](https://github.com/baskerville/bspwm),
[xmonad](https://xmonad.org), [evilwm](http://www.6809.org.uk/evilwm/),
[monsterwm-xcb](https://github.com/Cloudef/monsterwm-xcb),
[4wm](https://github.com/dct2012/4wm), and [frankenwm](https://github.com/sulami/FrankenWM).

