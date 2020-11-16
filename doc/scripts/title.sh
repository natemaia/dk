#!/bin/bash

# print the active window title

# using dkcmd

dkcmd status num=1 | grep '^A' | sed 's/^A//'


# using xprop

# active="$(xprop -root -f _NET_ACTIVE_WINDOW 0x " \$0\\n" _NET_ACTIVE_WINDOW | cut -d' ' -f2)"
# [[ $active ]] || exit 1
# title=$(xprop -id "$active" -f _NET_WM_NAME 0u " \$0\\n" _NET_WM_NAME | cut -d'"' -f2)
# if [[ -n $title ]]; then
# 	title=$(xprop -id "$active" -f WM_NAME 0u " \$0\\n" WM_NAME | cut -d'"' -f2)
# 	[[ $title ]] || exit 1
# fi

# echo "$title"
