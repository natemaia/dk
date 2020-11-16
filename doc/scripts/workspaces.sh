#!/bin/bash

# print workspace numbers
awk '/^workspaces:/ {sub(/^workspaces: /, ""); gsub(/:\w*/, ""); print}' <(dkcmd status type=full num=1)
