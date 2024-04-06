#!/bin/bash

# simple script to change dk border colour based on the workspace
# written by Nathaniel Maia, 2021

# example usage bind with sxhkd
# alt + {_,shift + ,ctrl + }{1-6}
#	 /path/to/script ws {view,send,follow} {1-6}

if (( $# == 0 )); then
	echo "usage: $0 [action] <gap_width>"
	exit 2
fi

currentws()
{
	dkcmd status type=ws num=1 | jq '.workspaces | .[] | select(.focused==true) | .number'
}

if [[ $1 =~ (view|send|follow) ]]; then
	[[ $1 == send ]] && { dkcmd ws "$1" "$2"; exit; }
	action="$1"
	shift
else
	action="send"
fi

(( $1 == $(currentws) )) && exit 0

case "$1" in
	[1-3])
		typeset -A col=(
		[f]='#6699cc'
		[u]='#ee5555'
		[uf]='#444444'
		[of]='#222222'
		[ou]='#222222'
		[ouf]='#222222'
		)
		;;
	[4-6])
		typeset -A col=(
		[f]='#ee5555'
		[u]='#6699cc'
		[uf]='#444444'
		[of]='#222222'
		[ou]='#222222'
		[ouf]='#222222'
		)
		;;
esac

dkcmd set border colour focus="${col[f]}" urgent="${col[u]}" unfocus="${col[uf]}" \
	outer_focus="${col[of]}" outer_urgent="${col[ou]}" outer_unfocus="${col[ouf]}" || exit
dkcmd ws "$action" "$1"
