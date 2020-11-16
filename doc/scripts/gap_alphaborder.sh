#!/bin/bash

# simple script to toggle dk border opacity based on the gap width
# written by Nathaniel Maia, 2020

# example usage bind with sxhkd
# alt + {equal,minus,apostrophe}
#	 /path/to/script {+5,-5,reset}

# first runs `dkcmd set gap width $1`, then checks to see what the current
# gap setting is if it's >=threshold we add slight border transparency

# does not have to be transparency were toggling, could be any setting where
# multiple states may be desired depending on the situation, but this is just
# an example

# alpha value to use for when borders should be semi-transparent, 00 - ff
# this will get changed to 'ff' when gap width is < threshold
alpha='80'

# gap width threshold for when to turn borders semi-transparent, 1 - N
# 0 would be always setting them semi-transparent, which is wasteful
thresh=10

# border colours, #000000 - #ffffff
typeset -A col=(
	[f]='#6699cc'
	[u]='#ee5555'
	[uf]='#444444'
	[of]='#222222'
	[ou]='#222222'
	[ouf]='#222222'
)

if (( $# == 0 )); then
	echo "usage: $0 <gap_width>"
	exit 2
fi


currentwsgap()
{
	awk '{
		if (!s && $1 == "workspaces:") {
			for (i = 1; i <= NF; i++) {
				if ($i ~ "*") {
					sub(/\*/, "");
					gsub(/:[a-z]* /, " ");
					s = $i;
				}
			}
		} else if (s && $1 == s) {
			print $7;
			exit;
		}
	}' <(dkcmd status type=full num=1)
}

# store the gap width before and after changing
old=$(currentwsgap)
dkcmd set gap width "$1"
ret=$? # if we don't cross the threshold this will be our exit code
new=$(currentwsgap)

# did we cross the threshold, if so we need to update the border colours alpha
if (( (old >= thresh && new < thresh) || (old < thresh && new >= thresh) )); then
	(( new < thresh )) && alpha='ff'
	for i in "${!col[@]}"; do
		c="${col[$i]}"
		col[$i]="${c/\#/\#$alpha}"
	done
	dkcmd set border colour focus="${col[f]}" urgent="${col[u]}" unfocus="${col[uf]}" \
		outer_focus="${col[of]}" outer_urgent="${col[ou]}" outer_unfocus="${col[ouf]}"
else
	exit $ret
fi
