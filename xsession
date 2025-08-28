#!/bin/sh
xset -dpms s off
setxkbmap fi 2>/dev/null || true
xrdb -merge ~/.Xresources 2>/dev/null || true

exec xterm -geometry 120x40+100+50 -bg black -fg white -e ~/head-software/run_system.sh
