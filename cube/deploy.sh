#!/bin/bash
# Push cube/ to the Pi, rebuild, install into /opt/ledcube and restart the service.
# Run from WSL:   bash cube/deploy.sh            (code + config)
#                 bash cube/deploy.sh --conf     (config only, applied live, no restart)
set -e
PI=${LEDCUBE_PI:-hrgaukel@192.168.50.66}
HERE=$(cd "$(dirname "$0")" && pwd)

if [ "$1" = "--conf" ]; then
  scp -q "$HERE/ledcube.conf" "$PI:/opt/ledcube/ledcube.conf"
  ssh "$PI" "python3 - <<'EOF'
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.settimeout(2)
s.sendto(b'C reload', ('127.0.0.1', 7777)); print(s.recv(600).decode())
EOF"
  exit 0
fi

rsync -a --exclude ledcube --exclude '*.exe' "$HERE/" "$PI:rgb-matrix/cube/"
ssh "$PI" 'set -e
  make -s -C ~/rgb-matrix/cube
  [ -d /opt/ledcube ] || { echo "built, but /opt/ledcube is missing: run setup.sh once"; exit 1; }
  install -m 755 ~/rgb-matrix/cube/ledcube /opt/ledcube/ledcube.new
  mv /opt/ledcube/ledcube.new /opt/ledcube/ledcube
  install -m 644 ~/rgb-matrix/cube/ledcube.conf /opt/ledcube/ledcube.conf
  sudo -n /usr/bin/systemctl restart ledcube
  sleep 2; systemctl is-active ledcube; journalctl -u ledcube -n 3 --no-pager -o cat | cut -c1-200'
