#!/bin/bash

# basic scratchpad functionality for dk
# spawns a set command with a known title if not already open
# if open toggle between the current workspace and the last
# written by Nathaniel Maia - 2021


# these can be edited by the user to be any window, just make sure to set the class
# to our title using whatever flag is needed: --class, -c, etc.
title="scratchpad"
cmd=(
	st -c "$title"
)


# window ID, we need to printf it as 8 hex digits to later match with dk status
win=$(printf '0x%08x' "$(xwininfo -root -children | awk '/'"$title"'/ {print $1}')")

if (( win != 0 )); then
	# window is already open so toggle it
	stat=$(dkcmd status num=1 type=full)
	ws=$(awk '/^workspaces:/ { for (i = 1; i <= NF; i++) { if ($i ~ "*") print i - 1 } }' <<< "$stat")
	wins=$(sed -n '/^windows:/,/^$/p' <<< "$stat")
	win_ws=$(grep "^\s*${win}" <<< "$wins" | awk -F'" ' '{print $4}' | cut -d' ' -f1)

	if (( win_ws == ws )); then
		# hide it
		# we could create a new workspace and place it there instead to not mess with the users existing workspaces
		dkcmd ws send "$win" "$(awk '/numws/{print $2}' <<< "$stat")"
	else
		# show it
		dkcmd ws send "$win" "$ws"
	fi
else
	# the window is not yet spawned so do so
	"${cmd[@]}" &>/dev/null & disown
fi
