### yaxwm

Yet another X window manager, as if we didn't have enough..

After using dwm for some time and changing it a lot *(and learning a lot)* I decided to try writing my own.
As is common with most of the dwm-based window managers, some of the code is based on (or straight ripped from) dwm,
so a can expect similar behavior and binds. I'm not afraid to say dwm is great and does so much right when
it comes to tiling window management.

Yaxwm differs in several ways:

- RANDR support.

- No built-in bar.

- Workspace centric.

- Based on xcb rather than xlib.

- Supports most [ewmh standards](https://specifications.freedesktop.org/wm-spec/wm-spec-latest.html).

- No SLOC restrictions yet.

- Likely more bugs/errors.


#### Installation

Run `make` as a normal user so the created config.h is editable, edit `config.h` to your liking, finally run `sudo make clean install`.

If at any time you want to uninstall it you can run `sudo make uninstall`.


#### Usage

To start yaxwm you can add `exec yaxwm` to your xinitrc.


#### Configuration

Copy the default config header `config.def.h` to `config.h` then edit it to suit your liking and recompile.


#### Todo

- More layouts?

- Text config and parser or fifo reader for better control without recompiling?

- Verify proper multi-head support*.

- Code simplifications and better error handling *(ongoing battle)*.


#### Contributing

I'm very open to contributions and input or ideas so feel free. I don't use a ton of comments and the xcb documentation is kinda shit,
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

See the LICENSE file for a list of authors/contributors.

Huge thanks to [dwm](https://dmw.suckless.org) and the suckless community for the awesome software and knowledge.

Also thanks to [4wm](https://github.com/dct2012/4wm), [awesome](https://github.com/awesomeWM/awesome),
[monsterwm-xcb](https://github.com/Cloudef/monsterwm-xcb), and [frankenwm](https://github.com/sulami/FrankenWM)
for helping me to understand some of the xcb library and window management in general.

Finally, thanks to the [xcb docs](https://xcb.freedesktop.org) for being one of the slowest websites of all time XD.
