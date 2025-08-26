

#Requirements osc2shaderstream
sudo pacman -S gst-plugins-base gst-plugins-good gst-plugins-bad gst-plugins-ugly gst-libav x264


#Test requirements
python -m venv .env
source .env/bin/activate.fish
pip install python-osc
