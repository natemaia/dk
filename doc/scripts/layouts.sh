#!/bin/sh

# print workspace layouts
awk '/^workspaces:/ {sub(/^workspaces: /, ""); gsub(/(*)?[0-9]*:\w*:/, ""); print}' "$DKSTAT"
