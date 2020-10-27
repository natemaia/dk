#!/bin/bash

# simple lemonbar script for use with dk

bg="#111111"
fg="#666666"
hi="#6699ee"
font="-*-terminus-medium-r-normal--*-200-*-*-*-*-iso10646-1"

# mimic dwm style layout symbols
typeset -A layouts=(
[tile]="[]="
[mono]="[M]"
[none]="><>"
[grid]="###"
[spiral]="(@)"
[dwindle]="[\\]"
)

workspaces()
{
	# list of which workspaces have windows on them
	a=$(awk '/^windows:/ {
		s = "";
		gsub(/^windows: |\*?#[0-9a-f]*:/, "");
		for (i = 1; i <= NF; i++) {
			if (s !~ $i) {
				printf "%s ", $i;
				s = s $i
			}
		}
	}' "$DKSTAT")

	# walk status and output workspace info with formatting
	awk -v fg="$fg" -v hi="$hi" -v a="$a" '/^workspaces:/ {
		gsub(/^workspaces: |:\w*/, "");
		for (i = 1; i <= NF; i++) {
			printf "%%{A:dkcmd ws %d:}", i
			if ($i ~ /*/) { /* active workspace*/
				gsub(/*/, "");
				if (a ~ $i) {
					printf " %%{+u}%%{F%s}%s%%{F%s}%%{-u} ", hi, $i, fg
				} else {
					printf " %%{F%s}%s%%{F%s} ", hi, $i, fg
				}
			} else if (a ~ $i) {
				printf " %%{+u}%s%%{-u} ", $i
			} else {
				printf " %s ", $i
			}
			printf "%%{A}"
		}
	}' "$DKSTAT"
}

layout()
{
	awk '/^workspaces:/ {
		gsub(/^workspaces: |[0-9]*:\w*:/, "");
		for (i = 1; i <= NF; i++) {
			if ($i ~ /*/) {
				sub(/*/, "");
				print $i
			}
		}
	}' "$DKSTAT"
}

battery()
{
	b=$(acpi --battery 2>/dev/null | cut -d, -f2 | tr -d '[:space:]')
	[[ -z $b ]] || echo -en " $b  - "
}

volume()
{
	pamixer --get-volume-human
}

clock()
{
	date "+%H:%M:%S  -  %a, %d %B %Y"
}

while sleep 0.3; do
	layout=${layouts[$(layout)]}
	echo -e "%{l} $(workspaces)  ${layout:-\???} %{r}$(battery) $(volume)  -  $(clock) "
done | lemonbar -B "$bg" -F "$fg" -f "$font" | sh

# vim:ft=sh:fdm=marker:fmr={,}
