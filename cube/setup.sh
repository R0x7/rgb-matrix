#!/bin/bash
# One-time root setup for the LED cube. Run on the Pi:
#     sudo bash ~/rgb-matrix/cube/setup.sh            (then reboot)
#     sudo bash ~/rgb-matrix/cube/setup.sh --no-desktop
#
# What it changes:
#   1. /opt/ledcube                    install dir owned by you, so deploys need no sudo
#   2. /etc/systemd/system/ledcube.service   runs the cube at boot (as root: GPIO needs it;
#                                      the program drops to user "daemon" once the pins are set up)
#   3. /etc/sudoers.d/ledcube          lets you start/stop/restart that one service without a
#                                      password. NOTE: since you own the binary the service runs,
#                                      this is effectively passwordless root for your user.
#   4. onboard audio off               (dtparam=audio=off + blacklist snd_bcm2835); the matrix
#                                      library uses the same hardware timer and refuses to share it
#   5. isolcpus=3 on the kernel command line: reserves a core for the panel refresh (less flicker)
#   6. --no-desktop only: boot to the console instead of the desktop. Frees a lot of CPU and RAM
#      on a Pi 3 and noticeably steadies the picture. Undo: sudo systemctl set-default graphical.target
set -e

[ "$(id -u)" = 0 ] || { echo "run me with sudo"; exit 1; }
U=${SUDO_USER:?run via sudo from your normal user}
HERE=$(cd "$(dirname "$0")" && pwd)
[ -x "$HERE/ledcube" ] || { echo "build first: make -C $HERE"; exit 1; }

install -d -o "$U" -g "$U" /opt/ledcube /opt/ledcube/fonts
install -o "$U" -g "$U" -m 755 "$HERE/ledcube" /opt/ledcube/ledcube
[ -f /opt/ledcube/ledcube.conf ] || install -o "$U" -g "$U" -m 644 "$HERE/ledcube.conf" /opt/ledcube/
for f in 5x7 6x10 10x20; do
  install -o "$U" -g "$U" -m 644 "$HERE/../rpi-rgb-led-matrix/fonts/$f.bdf" /opt/ledcube/fonts/
done

cat > /etc/systemd/system/ledcube.service <<EOF
[Unit]
Description=LED cube display
After=network.target

[Service]
WorkingDirectory=/opt/ledcube
ExecStart=/opt/ledcube/ledcube --config /opt/ledcube/ledcube.conf
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF

SC=/usr/bin/systemctl
echo "$U ALL=(root) NOPASSWD: $SC start ledcube, $SC stop ledcube, $SC restart ledcube" \
  > /etc/sudoers.d/ledcube
chmod 440 /etc/sudoers.d/ledcube
visudo -cf /etc/sudoers.d/ledcube >/dev/null || { rm -f /etc/sudoers.d/ledcube; echo "sudoers check failed"; exit 1; }

CFG=/boot/firmware/config.txt
CMD=/boot/firmware/cmdline.txt
cp -n "$CFG" "$CFG.before-ledcube"
cp -n "$CMD" "$CMD.before-ledcube"
sed -i 's/^dtparam=audio=on/dtparam=audio=off/' "$CFG"
echo "blacklist snd_bcm2835" > /etc/modprobe.d/blacklist-ledcube.conf
grep -q isolcpus "$CMD" || sed -i '1 s/$/ isolcpus=3/' "$CMD"

if [ "$1" = "--no-desktop" ]; then
  systemctl set-default multi-user.target
fi

systemctl daemon-reload
systemctl enable ledcube >/dev/null 2>&1
echo
echo "Done. Reboot to finish:  sudo reboot"
echo "The cube starts by itself after boot. Logs: journalctl -u ledcube -e"
