#!/bin/sh

# print workspace numbers
awk '/^workspaces:/ {sub(/^workspaces: /, ""); gsub(/:\w*/, ""); print}' "$DKSTAT"
