#!/bin/bash

# print workspace numbers
dkcmd status type=full num=1 | jq -r '.workspaces | .[] | [.number] | .[]'
