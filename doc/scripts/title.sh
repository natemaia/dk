#!/bin/bash

# print the active window title
dkcmd status type=win num=1 | sed 's/{.*:"\|"}$//g'
