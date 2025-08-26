#!/bin/bash
gst-launch-1.0 udpsrc port="5000" caps = "application/x-rtp, media=(string)video, encoding-name=(string)RAW, sampling=(string)BGR, depth=(string)8, width=(string)260, height=(string)180, payload=(int)96" ! rtpvrawdepay ! videoconvert ! queue ! xvimagesink sync=false
