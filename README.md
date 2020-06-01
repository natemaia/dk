### yaxwm

Yet another X window manager, as if we didn't have enough..

---

A from-scratch tiling window manager written in C using xcb. Features  
ideas from myself and other users as well as other window managers we've  
used and liked. After bouncing around various window managers for some years  
then using and hacking on dwm for a year or so I decided to try writing my  
own window manager.

Various feature/highlights:

- Does not require editing source code to customize.

- Intended to be hackable source and scriptable using shell commands.

- Startup script for configuring the wm and running programs upon startup.

- Based on xcb instead of xlib, randr extension for multi-head instead of xinerama.

- Dynamic workspaces similar to xmonad, with a workspace centric model at the core.

- Uses a Unix socket and commands to communicate with and control the window manager.

- No support for binding keys, this can be done with an external program like sxhkd.

- No built-in bar or font drawing, programs like [lemonbar](https://github.com/LemonBoy/bar),
[polybar](https://github.com/polybar/polybar), or [tint2](https://gitlab.com/o9000/tint2) work well.

- Mostly adheres to
[ICCCM](https://www.x.org/releases/X11R7.6/doc/xorg-docs/specs/ICCCM/icccm.html#client_to_window_manager_communication),
[EWMH](https://specifications.freedesktop.org/wm-spec/wm-spec-latest.html), and
[Motif](http://www.ist.co.uk/motif/books/vol6A/ch-20.fm.html#963509) for better
interoperability with other programs.

- This is still under active development so expect bugs/errors, please open an issue.


#### Installation

You need the xcb headers, if you're on an Arch based distro you can run

```
pacman -S xcb-proto xcb-util xcb-util-wm xcb-util-cursor xcb-util-keysyms
```
Other systems should have packages with similar names.

As mentioned above yaxwm has no key bind support, you'll need a separate  
program like sxhkd to launch programs and control the window manager.  
On an Arch based distro this can be installed using
```
pacman -S sxhkd
```


To compile run
```
make
```

Edit `yaxwm.h` if needed, then run *(as root if needed)*
```
make install
```

If at any time you want to remove yaxwm, run
```
make uninstall
```


#### Usage

To start yaxwm you can add the following to your `~/.xinitrc`
```
exec yaxwm
```

Optionally copy the example yaxwmrc and/or sxhkdrc to their  
respective locations
```
mkdir -p ~/.config/sxhkd ~/.config/yaxwm
cp /usr/local/share/doc/yaxwm/sxhkdrc ~/.config/sxhkd/
cp /usr/local/share/doc/yaxwm/yaxwmrc ~/.config/yaxwm/
chmod +x ~/.config/yaxwm/yaxwmrc
```

#### Configuration

There are example `yaxwmrc` and `sxhkdrc` files in `doc/` or  
`/usr/local/share/doc/yaxwm` after installation.

Yaxwm looks for a file in the following order
```
$YAXWMRC
$XDG_CONFIG_HOME/yaxwm/yaxwmrc
$HOME/.config/yaxwm/yaxwmrc
```
and tries to execute it, **it must be executable**.

Advanced changes and configuration like new layouts, callbacks, or new commands  
can be done by copying the default config header `yaxwm.def.h` to `yaxwm.h`,  
editing it and recompiling. This file isn't tracked by git so you can keep your  
configuration and avoid conflicts when pulling new updates.

#### Commands
Most if not all of your interaction with the window manager will be using  
commands in the form of
```
yaxwm -c ...
```
This will spawn a new instance of yaxwm to write our command into the socket  
where it can then be read and parsed by the operating yaxwm process.


###### Syntax Outline
Yaxwm commands have a very basic syntax and parsing, commands are broken  
down into smaller pieces *(tokens)* which are then passed to the keyword  
function.

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

For various commands yaxwm will expect a certain data type or format to be given.

- string: normal plain text, must be less than 256 characters.

- boolean: `true`, `false`, `1`, or `0`.

- hex: `(0x/#)XXXXXXXX`, used for window ids

- integer: `(+/-)1`, if it is preceded by a sign it is considered relative.

- float: `(+/-)0.1`, same as integer but must contain a decimal value.

- colour: `(0x/#)[AA]RRGGBB`, if no alpha channel is given the colour is opaque.

---



#### Todo

- Code reduction and simplification.


#### Contributing

I'm open to contributions or ideas so feel free. I don't use a ton of comments  
and the xcb documentation is kinda shit, but if you're familiar with other window  
managers most of it will be simple. If you're coming from xlib, most of the calls  
are easily translated to xcb with minor fiddling. There are some make flags I use  
often when testing.

To enable internal stderr debug output
```
make debug
```

To leave debug symbols in *(for debuggers: gdb, valgrind, etc.)*.
```
make nostrip
```


### Credits

See the LICENSE file for a list of authors/contributors.

Others that are not included in contributors but I owe a huge thanks to:
[dwm](https://dmw.suckless.org), [bspwm](https://github.com/baskerville/bspwm),
[evilwm](http://www.6809.org.uk/evilwm/), [monsterwm-xcb](https://github.com/Cloudef/monsterwm-xcb),
[4wm](https://github.com/dct2012/4wm), and [frankenwm](https://github.com/sulami/FrankenWM).

