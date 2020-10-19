### yaxwm

Yet another X window manager.

---

A tiling window manager taking inspiration from dwm, bspwm, xmonad, and more.  
Designed to be lightweight and simple while offering more features and  
more approachable for non-native coders than the likes of dwm, xmonad, and others.

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


#### Installation

You need the xcb headers, if you're on an Arch based distro you can run

```
pacman -S xcb-proto xcb-util xcb-util-wm xcb-util-cursor xcb-util-keysyms
```
Other systems should have packages with similar names.

As mentioned above yaxwm has no keybind support so you'll need a separate  
program like `sxhkd` to launch programs and control the window manager.
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
$YAXWMRC                        # user specified location, otherwise defined by the WM
$XDG_CONFIG_HOME/yaxwm/yaxwmrc  # expected location on systems with xdg
$HOME/.config/yaxwm/yaxwmrc     # fallback location when all else is lost
```
and to runs it, **it must be executable in order for this to happen**.

Advanced changes and configuration like new layouts, callbacks, or new commands  
can be done by copying the default config header `yaxwm.def.h` to `yaxwm.h`,  
editing it and recompiling. This file isn't tracked by git so you can keep your  
configuration and avoid conflicts when pulling new updates.

#### Commands
Most if not all of your interaction with the window manager will be using  
commands in the form of
```
yaxcmd <command>
```
This will spawn a new instance of yaxwm to write our command into the socket  
where it can then be read and parsed by the operating yaxwm process.


###### Syntax Outline
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

For various commands yaxwm will expect a certain data type or format to be given.

- string: normal plain text, must be less than 256 characters.

- boolean: `true`, `false`, `1`, or `0`.

- hex: `(0x/#)XXXXXXXX`, used for window ids

- integer: `(+/-)1`, if it is preceded by a sign it is considered relative.

- float: `(+/-)0.1`, same as integer but must contain a decimal value.

- colour: `(0x/#)[AA]RRGGBB`, hex value, if no alpha channel is given the colour is opaque.

---



#### Todo

- Simplification.


#### Contributing

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
[evilwm](http://www.6809.org.uk/evilwm/), [monsterwm-xcb](https://github.com/Cloudef/monsterwm-xcb),
[4wm](https://github.com/dct2012/4wm), and [frankenwm](https://github.com/sulami/FrankenWM).

