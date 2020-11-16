#!/bin/bash
# shellcheck disable=SC2059,SC2064,SC2086

# simple lightweight lemonbar script for use with dk


bg="#111111"
fg="#666666"
highlight="#6699ee"
underline=3
seperator="┃"

# xfonts
font0="-*-terminus-medium-r-normal--*-240-*-*-*-*-iso10646-1"
font1=""
font2=""
font3=""

# xft fonts
# font0="monospace:pixelsize=24"
# font1="Font Awesome 5 Brands:pixelsize=20"
# font2="icomoon:pixelsize=18"
# font3="Anonymice Nerd Font Mono:pixelsize=18"

fifo="/tmp/bar.fifo"

# mimic dwm style layout symbols
typeset -A layouts=(
[tile]="[]="
[mono]="[M]"
[none]="><>"
[grid]="###"
[spiral]="(@)"
[dwindle]="[\\]"
[tstack]="F^F"
)

clock()
{
	if [[ $1 ]]; then
		while :; do
			date +"%%{A1:$1:}T%a %H:%M%%{A}"
			sleep 10
		done
	else
		while :; do
			date +"T%a %H:%M"
			sleep 10
		done
	fi
}

battery()
{
	if [[ $1 ]]; then
		while :; do
			printf 'B%s\n' "%{A1:$1:}$(acpi --battery 2>/dev/null | cut -d, -f2 | tr -d '[:space:]')%{A}"
			sleep 10
		done
	else
		while :; do
			printf 'B%s\n' "$(acpi --battery 2>/dev/null | cut -d, -f2 | tr -d '[:space:]')"
			sleep 10
		done
	fi
}

volume()
{
	if [[ $1 ]]; then
		while :; do
			printf 'V%s\n' "%{A1:$1:}$(pamixer --get-volume-human)%{A}"
			sleep 0.2
		done
	else
		while :; do
			printf 'V%s\n' "$(pamixer --get-volume-human)"
			sleep 0.2
		done
	fi
}

parsefifo()
{
	typeset f='' b='' u='' wm='' time='' bat='' vol='' title='' layout=''

	while read -r line; do
		case $line in
			T*) time="${line#?}" ;;
			V*) vol="${line#?}" ;;
			B*) bat="${line#?}" ;;
			A*) title="${line#?}" ;;
			L*) l="${line#?}"; layout="${layouts[$l]}" ;;
			W*)
				wm='' IFS=':'
				set -- ${line#?}
				while (( $# > 0 )); do
					item=$1
					name=${item#?}
					case $item in
						A*) f="$highlight" b="$bg" u="$highlight" ;; # occupied   - focused
						a*) f="$fg" b="$bg" u="$highlight" ;;        # occupied   - unfocused
						I*) f="$highlight" b="$bg" u="$fg" ;;        # unoccupied - focused
						i*) f="$fg" b="$bg" u="$fg" ;;               # unoccupied - unfocused
					esac
					wm="$wm%{F$f}%{B$b}%{+u}%{U$u}%{A:dkcmd ws $name:} $name %{A}%{-u}%{B-}%{F-}"
					shift
				done
				;;
		esac
		printf "%s\n" "%{l}$wm $seperator $layout%{c}$title%{r}$bat $seperator $vol $seperator $time "
	done
}


# kill the process and cleanup if we exit or get killed
trap "trap - TERM; kill 0; rm -f '$fifo'" INT TERM QUIT EXIT

# make the fifo
[ -e "$fifo" ] && rm "$fifo"
mkfifo "$fifo"


# here we dump info into the FIFO, order does not matter things are parsed
# out using the first character of the line. Click commands for left button
# can be added by passing an argument containing the command (like volume below)
clock '' > "$fifo" &
battery '' > "$fifo" &
volume 'pavucontrol' > "$fifo" &
dkcmd status > "$fifo" &


# run the pipeline
parsefifo < "$fifo" | lemonbar -a 32 -u $underline -B "$bg" -F "$fg" -f "$font0" -f "$font1" -f "$font2" -f "$font3" | sh

# vim:ft=sh:fdm=marker:fmr={,}
