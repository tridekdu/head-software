echo "Kek!"
~/head-software/
sudo ~/head-software/GST2panels_SPImangel/spipanelmangel --pipeline 'udpsrc port="5000" caps = "application/x-rtp, media=(string)video, encoding-name=(string)RAW, sampling=(string)BGR, depth=(string)8, width=(string)260, height=(string)180, payload=(int)96" ! rtpvrawdepay ! videoconvert ! videoscale ! video/x-raw,format=RGB,width=26,height=9 ! appsink name=sink emit-signals=true sync=false max-buffers=1 drop=true'
