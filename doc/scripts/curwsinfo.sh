#!/bin/bash

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
		sub(/^\s*/, "");
		print;
		exit;
	}
}' <(dkcmd status type=full num=1)
