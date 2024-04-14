#!/bin/bash
# shellcheck disable=SC2059,SC2064,SC2086,SC2001,SC2016

# simple lightweight lemonbar script for use with dk

set -eE -o pipefail

bg="#111111" # background colour
fg="#666666" # foreground colour
hi="#6699ee" # highlight colour
ul=3         # underline thickness
sep="┃"      # separator text
font="-xos4-terminus-medium-r-normal--12-240-72-72-c-120-iso10646-1"
fifo="/tmp/bar.fifo"

# adjust font size based on resolution DPI
px=$(xrandr | grep ' connected' | tail -n1 | grep -o '[0-9]\+x[0-9]\+' | cut -d'x' -f2)
mm=$(xrandr | grep ' connected' | tail -n1 | grep -o '[0-9]\+mm' | tail -n1 | sed 's/mm//')
dpi=$(( (px / mm) * 25 ))
if (( dpi >= 140 )); then
	font="-xos4-terminus-medium-r-normal--24-240-72-72-c-120-iso10646-1"
elif (( dpi >= 120 )); then
	font="-xos4-terminus-medium-r-normal--18-240-72-72-c-120-iso10646-1"
elif (( dpi >= 100 )); then
	font="-xos4-terminus-medium-r-normal--14-240-72-72-c-120-iso10646-1"
fi

# mimic dwm style layout symbols
typeset -A layouts=(
[tile]="[]="
[rtile]="=[]"
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
		date +"T%%{A1:$1:} %a %H:%M $sep%%{A}"
	else
		date +"T %a %H:%M $sep"
	fi

	# sync up the clock to the minute mark
	min=$(date +"%M")
	while [[ $(date +"%M") == "$min" ]]; do
		sleep 1
	done

	if [[ $1 ]]; then
		while :; do
			date +"T%%{A1:$1:} %a %H:%M $sep%%{A}"
			sleep 60
		done
	else
		while :; do
			date +"T %a %H:%M $sep"
			sleep 60
		done
	fi
}

battery()
{
	lvl=$(acpi --battery 2>/dev/null | grep -v 'Unknown\| 0%' | cut -d, -f2 | tr -d '[:space:]')
	[[ ! $lvl ]] && return # no battery so we don't need to continue

	if [[ $1 ]]; then
		while :; do
			bat="$(acpi --battery 2>/dev/null | grep -v 'Unknown\| 0%' | cut -d, -f2 | tr -d '[:space:]')"
			if [[ $lastb != "$bat" ]]; then
				lastb="$bat"
				printf 'B%s\n' "%{A1:$1:} Bat: ${bat} %{A}${sep}"
			fi
			sleep 120
		done
	else
		while :; do
			bat="$(acpi --battery 2>/dev/null | grep -v 'Unknown\| 0%' | cut -d, -f2 | tr -d '[:space:]')"
			if [[ $lastb != "$bat" ]]; then
				lastb="$bat"
				printf 'B%s\n' " Bat: ${bat} ${sep}"
			fi
			sleep 120
		done
	fi
}

volume()
{
	if [[ $1 ]]; then
		while :; do
			vol="$(pamixer --get-volume-human)"
			if [[ $lastv != "$vol" ]]; then
				lastv=$vol
				printf 'V%s\n' "%{A1:$1:} vol: $vol %{A}${sep}"
			fi
			sleep 1
		done
	else
		while :; do
			vol="$(pamixer --get-volume-human)"
			if [[ $lastv != "$vol" ]]; then
				lastv=${vol}
				printf 'V%s\n' " vol: ${vol} ${sep}"
			fi
			sleep 1
		done
	fi
}

network()
{
	check()
	{
		if hash nm-online > /dev/null 2>&1 && [[ $(systemctl is-active NetworkManager.service) == "active" ]]; then
			nm-online > /dev/null 2>&1
		else
			ping -qc1 'archlinux.org' > /dev/null 2>&1
		fi
	}

	if [[ $1 ]]; then
		while :; do
			printf 'N%s\n' "%{A1:$1:} disconnected %{A}${sep}"
			until check; do
				sleep 30
			done
			printf 'N%s\n' "%{A1:$1:} connected %{A}${sep}"
			while :; do
				sleep 300
				check || break
			done
		done
	else
		while :; do
			printf 'N%s\n' " disconnected ${sep}"
			until check; do
				sleep 30
			done
			printf 'N%s\n' " connected ${sep}"
			while :; do
				sleep 300
				check || break
			done
		done
	fi
}

workspaces()
{
	# these will be filled out once we eval each line
	typeset name="" title="" layout="" focused=false active=false

	WS=""
	while read -r l; do
		eval "$l"
		if [[ $l =~ ^title=* ]]; then
			# default foreground, background, and underline colours
			# changing the foreground and underline based on
			# the active and focused state
			f="$fg" b="$bg" u="$fg";
			$active && u="$hi"
			if $focused; then
				# clicking the layout symbol will cycle the layout
				lyt="%{A:dkcmd set layout cycle:}${layouts[$layout]}%{A}"
				WIN="${title:0:50}"
				f="$hi"
			fi
			# clicking on a workspace name will view it
			WS="$WS%{F$f}%{B$b}%{+u}%{U$u}%{A:dkcmd ws $name:} $name %{A}%{-u}%{B-}%{F-}"
		fi
		# turn the dk JSON output into lines that can be `eval`ed one by one,
		# filling out the following fields: name, focused, active, layout, title
	done < <(dkcmd -p <<< "$1" | sed '/.*\[\|]/d; /[{}],\?/d; s/^\s*\|,$//g; /"monitor":\|"number":\|"id":/d; s/"\(.*\)": /\1=/; s/\$(/\\\$(/g; s/`/\\`/g')
	WS="$WS $sep $lyt"
}

parsefifo()
{
	# globals to simplify the workspaces call
	declare -g WS WIN

	local time vol bat net

	while read -r line; do
		case $line in
			T*) time="${line#?}" ;;
			V*) vol="${line#?}" ;;
			B*) bat="${line#?}" ;;
			N*) net="${line#?}" ;;
			'{'*) workspaces "$line" ;;
		esac
		printf "%s\n" "%{l}$sep ${WS}%{c}${WIN}%{r}${net}${bat}${vol}${time}"
	done
}

# kill the process and cleanup if we exit or get killed
trap "trap - TERM; rm -f '$fifo'; kill 0" INT TERM QUIT EXIT PIPE

# make the fifo
[ -e "$fifo" ] && rm "$fifo"
mkfifo "$fifo"

# here we dump info into the FIFO, order does not matter things are parsed
# out using the first character of the line. Click commands for left button
# can be added by passing an argument containing the command (like below)
#
# comment a line to remove the "module"
network '' > "$fifo" &
clock 'gsimplecal' > "$fifo" &
battery '' > "$fifo" &
volume 'pavucontrol' > "$fifo" &
dkcmd status type=bar > "$fifo" &

# run the pipeline
if [[ $1 == '-b' ]]; then
	parsefifo < "$fifo" | lemonbar -b -a 32 -u $ul -B "$bg" -F "$fg" -f "$font" | sh;
else
	parsefifo < "$fifo" | lemonbar -a 32 -u $ul -B "$bg" -F "$fg" -f "$font" | sh
fi

# vim:ft=sh:fdm=marker:fmr={,}
