### yaxwm

Yet Another X Window Manager

After using dwm for a long time and making a lot of my own modifications *(and learning a fair bit of C)* I decided
to try writing my own. A large portion of the code is based on dwm so most behavior is the same *(for now)*.
In terms of features, yax follows the freedesktop [ewmh](https://specifications.freedesktop.org/wm-spec/wm-spec-latest.html)
specification for the most part, this makes incorporating external programs possible *(status bars and the like)*.

Currently there are no set goals or limitations for what I want to achieve with this so suggestions and PRs are welcome.


#### Installation

Run `make clean install` *(as root if needed)*.


#### Usage

To start yaxwm you can add `exec yaxwm` to your xinitrc.


#### Configuration

Copy the default config header `config.def.h` to `config.h`* then edit it to suit your liking and recompile.


#### Todo

- Multihead support **(partianlly complete, xrandr support is implimented)**

- Simplifications and better error handling

- Extra layouts: dual/dynamic stack

- Config parser?

- Fifo reader?


#### Contributing

As stated above I'm open to PRs and input. I don't use a ton of comments and xcb's documentation is kinda shit,
but if you're familiar with other window managers most of it will be simple. If you're coming from Xlib, most of
the calls are easily translated to xcb.

There are some make flags for extra output and leaving in debug symbols *(for debuggers: gdb, valgrind, etc.)*


To enable stderr debug output
```
make DFLAGS='-DDEBUG'
```

To leave debug symbols in the final executable
```
make DFLAGS='-DNOSTRIP'
```

### Credits
Huge thanks to [dwm](https://dmw.suckless.org) and the suckless community.

Also thanks to [i3wm](https://github.com/i3/i3).

