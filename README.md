### yaxwm

Yet another X window manager, as if we didn't have enough..

After using dwm for a long time, changing it, and learning. I wanted
to write my own window manager. I'm not afraid to say dwm is great and does
a lot of things right when it comes to simplifying window management.
At this point yaxwm is not solely based on dwm but more of a collection of
features and ideas in other window managers that I liked.

Some ways in which yaxwm differs include:

- Using xcb over xlib and randr support for multi-head, no xinerama support planned.

- Dynamic workspaces similar to xmonad, with a workspace centric model at the core.

- Simple startup script for easy configuration once the window manger is running.

- Unix socket communication for controlling and configuring the wm similar to bspwm.

- No built in method for binding keys, you'll need an external program like sxhkd.

- Supports more [ewmh standards](https://specifications.freedesktop.org/wm-spec/wm-spec-latest.html)
for better interoperability with other programs and applications.

- This is still under active development so expect bugs/errors, please open an issue or contact me.


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


#### Configuration

There are example yaxwmrc and sxhkdrc in `doc/` or `/usr/share/doc/yaxwm` after
installation.

Yaxwm looks for a file `$YAXWM_CONF`, `$XDG_CONFIG_HOME/yaxwm/yaxwmrc`, or
`$HOME/.config/yaxwm/yaxwmrc` and tries to execute it. This file is just a shell
script, it must be executable.

For advanced configuration like layout and callback functions, copy the default
config header `src/config.def.h` to `src/config.h` then edit it and recompile.
This file isn't tracked by git so you can maintain configuration across updates.


#### Todo

- Simplify.

- Improve/finish commands.


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

