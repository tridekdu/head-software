#!/bin/sh
gcc -O2 -Wall -o spipanelmangel SPIpanelmangel.c \
  $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0) -lpthread -lm

