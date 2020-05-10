### yaxwm

Yet another X window manager, as if we didn't have enough..


After using dwm for a long time, changing it, and learning from it; I wanted  
to write my own window manager. Dwm is great and it does a lot of things right  
when it comes to simplifying window management. Yaxwm is not a fork of dwm nor  
is it solely based on dwm, more of a collection of features and ideas from  
various window managers that I liked.

Some ways in which yaxwm differs include:

- Based on xcb instead of xlib, randr extension for multi-head instead of xinerama.

- Dynamic workspaces similar to xmonad, with a workspace centric model at the core.

- Startup script for easy configuration once the window manger is running.

- Like bspwm, yaxwm uses Unix socket communication for control and configuration.

- No support for binding keys, this can be done with an external program like sxhkd.

- Supports most
[ICCCM](https://www.x.org/releases/X11R7.6/doc/xorg-docs/specs/ICCCM/icccm.html#client_to_window_manager_communication),
[EWMH](https://specifications.freedesktop.org/wm-spec/wm-spec-latest.html), and
[Motif](http://www.ist.co.uk/motif/books/vol6A/ch-20.fm.html#963509) for better
interoperability with other programs and applications.

- This is still under active development so expect bugs/errors, please open an issue.


#### Installation

You need the xcb headers, if you're on Arch Linux you can run

```
pacman -S xcb-proto xcb-util xcb-util-wm xcb-util-cursor xcb-util-keysyms
```
Other systems should have packages with similar names.

As mentioned above, yaxwm has no key binds outside of mouse movement so  
you'll need an external program like sxhkd to launch programs and control  
the window manager.


To compile run
```
make
```

Edit `src/config.h` to your liking, then run *(as root if needed)*
```
make install
```

If at any time you want to remove yaxwm, run
```
make uninstall
```


#### Usage

To start yaxwm you can add `exec yaxwm` to your xinitrc.

Optionally copy the default yaxwmrc and sxhkdrc to their respective locations

```
mkdir -p ~/.config/sxhkd ~/.config/yaxwm
cp /usr/share/doc/yaxwm/sxhkdrc ~/.config/sxhkd/
cp /usr/share/doc/yaxwm/yaxwmrc ~/.config/yaxwm/
chmod +x ~/.config/yaxwm/yaxwmrc
```

#### Configuration

There are example `yaxwmrc` and `sxhkdrc` files in `doc/` or `/usr/share/doc/yaxwm`  
after installation.

Yaxwm looks for a file in the following order
```
$YAXWM_CONF
$XDG_CONFIG_HOME/yaxwm/yaxwmrc
$HOME/.config/yaxwm/yaxwmrc
```
and tries to execute it, **it must be executable**.

Further configuration like new layout and callback functions can be done by
copying he default config header `src/config.def.h` to `src/config.h`, editing  
it and recompiling. This file isn't tracked by git so you can maintain  
configuration across updates.  

#### Commands

###### Syntax Outline
Yaxwm includes a small program `yaxcmd` to communicate with the window manager,  
this is what you will be interacting with. The commands have a very basic syntax  
and parsing system, commands are broken down into smaller pieces *(tokens)*.

A token is delimited by one or more:

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

- integer: `(+/-)1`, when the number is preceded by a sign it is considered relative.

- float: `(+/-)0.1`, same as integer but must contain a decimal value.

- colour: `(0x/#)(AA)RRGGBB`, when no alpha channel is given the colour will be opaque.


#### Todo

- Simplify.


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
[monsterwm-xcb](https://github.com/Cloudef/monsterwm-xcb),
[4wm](https://github.com/dct2012/4wm), and [frankenwm](https://github.com/sulami/FrankenWM).

