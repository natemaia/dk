#!/bin/bash

# show current workspace in a dzen2 window


width=100  # enough to fit one character plus padding
height=100 # roughly font size * 3, e.g. 36pt * 3 = 108 = 100px
font="Liberation:size=36:style=bold"

if ! hash dzen2 >/dev/null 2>&1; then
	echo "requires dzen2 installed" >&2
	exit 1
fi

stat=$(dkcmd status type=full num=1)

# get current monitor width and height
typeset -i w=0 h=0
eval "$(awk '{
	if (!s && $1 == "monitors:") {
		for (i = 1; i <= NF; i++) {
			if ($i ~ "*") {
				gsub(/\*|:[0-9]+/, "");
				s = $i;
			}
		}
	} else if (s && $1 == s) {
		print "w="$5, "h="$6;
		exit;
	}
}' <<< "$stat")"

# determine current workspace number
WS=$(awk '/^workspaces:/ {
	for (i = 1; i <= NF; i++) {
		if ($i ~ "*")
			print i - 1
	}
}' <<< "$stat")

# find center of the screen
x=$(( (w / 2) - (width / 2) ))
y=$(( (h / 2) - (height / 2) ))

dzen2 -fn "$font" -p 1 -x $x -y $y -tw $width <<< "$WS"
