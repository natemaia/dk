#!/bin/bash

# print workspace layouts
dkcmd status type=full num=1 | jq -r '.workspaces | .[] | [.layout] | .[]'
