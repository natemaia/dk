#!/bin/bash
# shellcheck disable=SC2059,SC2064

# simple lemonbar script for use with dk


bg="#111111"
fg="#666666"
highlight="#6699ee"
underline=3
font0="monospace:pixelsize=24"
font1="Font Awesome 5 Brands:pixelsize=20"
font2="icomoon:pixelsize=18"
font3="Anonymice Nerd Font Mono:pixelsize=18"

fifo="/tmp/bar.fifo"

# mimic dwm style layout symbols
typeset -A layouts=(
[tile]="[]="
[mono]="[M]"
[none]="><>"
[grid]="###"
[spiral]="(@)"
[dwindle]="[\\]"
[tstack]="ꓕꓕꓕ"
)

title()
{
	typeset fmt="$1"

	while :; do
		a=$(xprop -root -f _NET_ACTIVE_WINDOW 0x " \$0\\n" _NET_ACTIVE_WINDOW 2>/dev/null | cut -d' ' -f2)
		if [[ $a ]]; then
			t="$(xprop -id "$a" -f _NET_WM_NAME 0u " \$0\\n" _NET_WM_NAME 2>/dev/null | cut -d'"' -f2)"
			if [[ -z $t || $t == '_NET_WM_NAME:  not found.' ]]; then
				t="$(xprop -id "$a" -f WM_NAME 0u " \$0\\n" WM_NAME | cut -d'"' -f2)"
			fi
			if [[ $t && $t != 'WM_NAME:  not found.' ]]; then
				if [[ $2 ]]; then
					printf "$fmt" "%{A1:$2:}$t%{A}"
				else
					printf "$fmt" "$t"
				fi
			else
				printf "$fmt" ""
			fi
		else
			printf "$fmt" ""
		fi
		sleep 1
	done
}

clock()
{
	typeset fmt="$1"

	if [[ $2 ]]; then
		while :; do
			date +"%%{A1:$2:}$fmt%%{A}"
			sleep 10
		done
	else
		while :; do
			date +"$fmt"
			sleep 10
		done
	fi
}

battery()
{
	typeset fmt="$1"

	if [[ $2 ]]; then
		while :; do
			printf "$fmt" "%{A1:$2:}$(acpi --battery 2>/dev/null | cut -d, -f2 | tr -d '[:space:]')%{A}"
			sleep 10
		done
	else
		while :; do
			printf "$fmt" "$(acpi --battery 2>/dev/null | cut -d, -f2 | tr -d '[:space:]')"
			sleep 10
		done
	fi
}

volume()
{
	typeset fmt="$1"

	if [[ $2 ]]; then
		while :; do
			printf "$fmt" "%{A1:$2:}$(pamixer --get-volume-human)%{A}"
			sleep 0.2
		done
	else
		while :; do
			printf "$fmt" "$(pamixer --get-volume-human)"
			sleep 0.2
		done
	fi
}

parsefifo()
{
	# parse the input line by line and case each based on the first character
	# once finished reading each line print the output built so far

	typeset f='' b='' u='' wm='' time='' bat='' vol='' title=''

	while read -r line; do
		case $line in
			T*) time="${line#?} " ;;
			V*) vol="${line#?} " ;;
			B*) bat="${line#?} " ;;
			A*) title="${line#?} " ;;
			W*) # `dkcmd status` is prefixed with "W"
				wm=''
				IFS=':'
				# shellcheck disable=SC2086
				set -- ${line#?}
				while (( $# > 0 )); do
					item=$1
					name=${item#?}
					case $item in
						[AaIi]*)
							case $item in
								A*) f="$highlight" b="$bg" u="$highlight" ;; # occupied   - focused
								a*) f="$fg" b="$bg" u="$highlight" ;;        # occupied   - unfocused
								I*) f="$highlight" b="$bg" u="$fg" ;;        # unoccupied - focused
								i*) f="$fg" b="$bg" u="$fg" ;;               # unoccupied - unfocused
							esac
							wm="$wm%{F$f}%{B$b}%{+u}%{U$u}%{A:dkcmd ws $name:} $name %{A}%{-u}%{B-}%{F-}"
							;;
						L*) # layout
							l=${layouts[$name]}
							wm="$wm%{F$fg}%{B$bg} ${l:-???} %{B-}%{F-}"
							;;
					esac
					shift
				done
				;;
		esac
		printf "%s\n" "%{l}${wm}%{c}${title}%{r}${bat}${vol}${time}"
	done
}


# kill the process and cleanup if we exit or get killed
trap "trap - TERM; kill 0; rm -f '$fifo'" INT TERM QUIT EXIT

# make the fifo
[ -e "$fifo" ] && rm "$fifo"
mkfifo "$fifo"

# here we dump info into the FIFO, order does not matter
# things are parsed out using the first character of the line
# so "TSat 16:14" will be parsed as "T" -> "Sat 16:14"
# click commands for left button can be added by passing
# a second argument containing the command
clock 'T%a %H:%M' > "$fifo" &
battery 'B%s\n' > "$fifo" &
volume 'V%s\n' 'pavucontrol' > "$fifo" &
title 'A%s\n' > "$fifo" &
dkcmd status > "$fifo" &

# run the pipeline
parsefifo < "$fifo" | lemonbar -a 32 -u $underline -B "$bg" -F "$fg" -f "$font0" -f "$font1" -f "$font2" -f "$font3" | sh

# vim:ft=sh:fdm=marker:fmr={,}
