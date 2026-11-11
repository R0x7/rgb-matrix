# LED cube

Three 64x64 HUB75 panels on a Raspberry Pi 3B+ (Adafruit HAT, chained), arranged as a cube
corner: top, left and right faces.

| Folder | What |
| --- | --- |
| `rpi-rgb-led-matrix/` | hzeller's panel driver library (vendored) |
| `cube/` | `ledcube`, the program that runs on the Pi |
| `pc/` | Windows side: stats/audio sender and `cubectl` remote control |

## What it shows

Auto mode rotates through a playlist and cross-fades between scenes:

- **Ambient** `plasma` `ripples` `lava` `rain` `life` `warp` `comets` `fire` `fireworks` `vortex`
  `kaleido` `orbs` `storm` `tank`. These are computed in 3D on the cube, so they run across the
  edges without a seam: flames lick over the top edge, firework sparks spill down the sides,
  `vortex` and `kaleido` are centred on the shared corner, and `orbs` and `tank` treat the cube
  as a glass box (bouncing balls / sloshing water).
- **`clock`** hours on the left face, minutes on the right, date and weather on top.
- **`stats`** this PC's CPU / GPU / RAM / network. Only in rotation while the sender is running.
- **`visualizer`** takes over whenever the PC is playing sound, and hands back when it stops.

## Pi setup (once)

```bash
make -C ~/rgb-matrix/cube
sudo bash ~/rgb-matrix/cube/setup.sh --no-desktop
sudo reboot
```

`setup.sh` lists exactly what it changes at the top of the file. After that, deploy changes from
WSL with `bash cube/deploy.sh` (or `--conf` for config only).

### Telling it which panel is which

`pc\cubectl.ps1 scene=identify` shows each panel's chain number and an arrow at that panel's own
"up". Put what you see into `cube/ledcube.conf` as `face = <number - 1>:<rotation>`, where the
rotation is where the arrow points:

| Arrow points | Side faces | Top face |
| --- | --- | --- |
| 0 | up | away from the left face |
| 90 | right | toward the right face |
| 180 | down | toward the left face |
| 270 | left | away from the right face |

Then `scene=calibrate` should show TOP / LEFT / RIGHT upright with matching edge colours.

## PC side

```powershell
.\pc\build.ps1                  # builds cube-sender.exe (uses the C# compiler built into Windows)
.\pc\install-startup.ps1        # run at login, hidden
.\pc\cubectl.ps1 status
.\pc\cubectl.ps1 scene=lava     # pin a scene;  auto | next | off | on | toggle | brightness=30 | reload
.\pc\cube off                   # same as cubectl.ps1, but works when Windows blocks .ps1 scripts
.\pc\install-toggle.ps1        # desktop shortcut: double-click to turn the cube off / back on
```

The sender sends stats and a 32-band spectrum of the default output device. Audio itself never
leaves the PC.

## Config

Everything is in `cube/ledcube.conf`: brightness, night dimming, playlist and timings, 12/24h,
and `latitude` / `longitude` for the weather (from open-meteo.com; blank = no weather).

## Preview without the panels

```bash
./ledcube --config ledcube.conf --dump out.ppm --scene plasma --frames 200
python3 tools/preview.py out.png out.ppm
```
