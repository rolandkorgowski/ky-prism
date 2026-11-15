#!/bin/bash
# Skrypt środowiska dla Claude Code w chmurze (claude.ai/code, aplikacja Claude).
# Wklej go w ustawieniach środowiska, w polu "Setup script". Uruchamia się jako
# root na Ubuntu 24.04 przed sesją; wynik jest zapamiętywany na kilka dni.
# Ta sama lista pakietów co w dependencies.py (rodzina debian) + Xvfb do testu okna.
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q --no-install-recommends \
  build-essential cmake pkg-config python3 git \
  qt6-base-dev qt6-base-dev-tools libqt6opengl6-dev \
  libgtk-4-dev libepoxy-dev libvips-dev libraw-dev liblcms2-dev libjpeg-dev zlib1g-dev \
  libglvnd-dev libegl-dev libgl-dev libgl1-mesa-dri \
  util-linux udisks2 desktop-file-utils xdg-utils \
  xvfb xauth
