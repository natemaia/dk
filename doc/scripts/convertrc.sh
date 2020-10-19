#!/bin/bash

# small script to convert old rc files to use the new (old) yaxcmd

sed -i 's/yaxwm -c/yaxcmd/g' ~/.config/{sxhkd/sxhkdrc,yaxwm/yaxwmrc}
