#!/bin/bash

# print workspace layouts
awk '/^workspaces:/ {sub(/^workspaces: /, ""); gsub(/(*)?[0-9]*:\w*:/, ""); print}' <(dkcmd status type=full num=1)
