### yaxwm

Yet another X window manager, as if we didn't have enough..

After using dwm for some time and changing it a lot *(and learning a lot)* I decided to try writing my own.
As is common with most of the dwm-based window managers, some of the code is based on (or straight ripped from) dwm,
so a can expect similar behavior and binds. I'm not afraid to say dwm is great and does so much right when
it comes to tiling window management.

Yaxwm differs in several ways:

- No built-in bar.

- Based on xcb rather than xlib.

- Supports most [ewmh standards](https://specifications.freedesktop.org/wm-spec/wm-spec-latest.html).

- Likely more bugs/errors.

---

Currently I don't have a ton of goals for what I want to achieve with this so suggestions and PRs are welcome.


#### Installation

Run `make` as a normal user so the created config.h is editable, edit `config.h` to your liking, finally run `sudo make clean install`.

If at any time you want to uninstall it you can run `sudo make uninstall`.


#### Usage

To start yaxwm you can add `exec yaxwm` to your xinitrc.


#### Configuration

Copy the default config header `config.def.h` to `config.h` then edit it to suit your liking and recompile.


#### Todo

- Manpage and more ducumentation!!

- More layouts: dual/dynamic stack, monocle, etc.

- Text config and parser or fifo reader for better control without recompiling?

- Confirm multihead support *(partially complete, randr is implemented but needs further testing on different systems.. Help!)*

- Code simplifications and better error handling *(ongoing battle)*


#### Contributing

As mentioned above I'm open to PRs and input/ideas. I don't use a ton of comments and the xcb documentation is kinda shit,
but if you're familiar with other window managers most of it will be simple. If you're coming from xlib, most of
the calls are easily translated to xcb with minor fiddling. There are some make flags I use often when testing.

To enable stderr debug output
```
make DFLAGS='-DDEBUG'
```
Then you can start yaxwm and redirect stderr to a file for tailing with `tail -f` or parsing.
```
exec yaxwm 2> ~/.yaxwm.log
```

To leave debug symbols in the final executable *(for debuggers: gdb, valgrind, etc.)*.
```
make DFLAGS='-DNOSTRIP'
```

### Credits
Huge thanks to [dwm](https://dmw.suckless.org) and the suckless community for the awesome software and knowledge.

Also thanks to [4wm](https://github.com/dct2012/4wm), [awesome](https://github.com/awesomeWM/awesome),
[monsterwm-xcb](https://github.com/Cloudef/monsterwm-xcb), and [frankenwm](https://github.com/sulami/FrankenWM)
for helping me to understand some of the xcb library and window management in general.

Finally, thanks to the [xcb docs](https://xcb.freedesktop.org) for being one of the slowest websites of all time XD.
