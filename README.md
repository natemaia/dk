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
Edit the source and recompile.

### Credits
Obviously a huge thanks to [dwm](https://dmw.suckless.org)


[i3wm](https://github.com/i3/i3) for some of the less documented xcb ewmh/icccm setup.


[mcwm](https://github.com/mchackorg/mcwm)

