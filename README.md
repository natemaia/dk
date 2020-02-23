### yaxwm

Yet another X window manager, as if we didn't have enough..

After using dwm for some time and changing it a lot *(and learning a lot)*
I decided to try writing my own. I'm not afraid to say dwm is great
and does so much right when it comes to tiling window management.

Yaxwm differs in several ways:

- RANDR support.

- No built-in bar.

- Workspace centric.

- Likely more bugs/errors.

- Supports more [ewmh standards](https://specifications.freedesktop.org/wm-spec/wm-spec-latest.html).

- Based on xcb rather than xlib.

- No source lines restrictions yet.

- Fifo pipe communication for controlling and configuring the wm.

- Simple startup script for easy configuration once the wm is running.

- No Keybinds, control can be done fifo commands and an external program.


#### Installation

###### Requirements
You will need various xcb headers, on Arch Linux you can run

```
pacman -S xcb-proto xcb-util xcb-util-wm xcb-util-cursor xcb-util-keysyms
```
On other systems packages with similar names should be available.


Furthermore yaxwm offers no key binds so you will need a third party
program like `sxhkd` in order to launch programs and control the wm.


###### Build
To compile run
```
make
```

Edit `src/config.h` to your liking, then to install run
```
sudo make clean install
```

If at any time you want to remove yaxwm, you can run `sudo make uninstall`.


#### Usage

To start yaxwm you can add `exec yaxwm` to your xinitrc.


#### Configuration

Copy the default config header `src/config.def.h` to `src/config.h`
then edit it to suit your liking and recompile.

There is an example yaxwmrc and sxhkdrc in the `doc/`, or `/usr/share/doc/yaxwm` once installed.

Yaxwm looks for a file `YAXWM_CONF`, `XDG_CONFIG_HOME/yaxwm/yaxwmrc`, or `HOME/.config/yaxwm/yaxwmrc`
and tries to execute it, this file is just a shell script and must be executable.


#### Todo

- Simplify.

- Improve/finish commands.


#### Contributing

I'm open to contributions or ideas so feel free. I don't use a ton of comments and the xcb documentation is kinda shit,
but if you're familiar with other window managers most of it will be simple. If you're coming from xlib, most of
the calls are easily translated to xcb with minor fiddling. There are some make flags I use often when testing.

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

Huge thanks to:
- [dwm](https://dmw.suckless.org) and the suckless community for the awesome software and knowledge.

- [bspwm](https://github.com/baskerville/bspwm) for some of the cool ideas for wm design.

- [monsterwm-xcb](https://github.com/Cloudef/monsterwm-xcb)

- [awesome](https://github.com/awesomeWM/awesome)

- [frankenwm](https://github.com/sulami/FrankenWM)

- [4wm](https://github.com/dct2012/4wm)

- [xcb docs](https://xcb.freedesktop.org) for being one of the slowest websites of all time XD.

