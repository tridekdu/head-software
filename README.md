

#Requirements osc2shaderstream
sudo pacman -S gst-plugins-base gst-plugins-good gst-plugins-bad gst-plugins-ugly gst-libav x264 opencv liblo


#Test requirements
python -m venv .env
source .env/bin/activate.fish
pip install python-osc





# System config

pacman -S --needed xorg-server xorg-xinit xorg-xauth mesa libglvnd xf86-input-libinput xterm xorg-xrandr sddm x11vnc gst-plugins-bad

sudo systemctl edit getty@tty1
```
[Service]
ExecStart=
ExecStart=-/usr/bin/agetty --autologin USER --noclear %I $TERM
Type=idle
```

/usr/share/xsessions/32-x11.desktop
```
[Desktop Entry]
Name=32X11
Exec=/home/tridekdu/head-software/xsession
Type=Application
```

/etc/sddm.conf
```
[Autologin]
User=tridekdu
Session=32-x11
```
