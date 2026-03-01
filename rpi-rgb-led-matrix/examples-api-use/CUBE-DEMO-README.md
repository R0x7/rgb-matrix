# 3D Cube LED Display Script

This script displays cohesive images across three LED screens arranged in a 3D cube formation.

## Overview

The `cube-demo` application is designed to display unified animations and visuals on three LED panels arranged as visible faces of a cube:

```
     [Top Face]
[Front Face][Right Face]
```

The script handles the pixel mapping automatically, ensuring that patterns flow seamlessly across the cube's surfaces.

## Hardware Setup

You need:
- **3 RGB LED Panels** (e.g., 32x32 or 16x32): One each for front, right, and top faces
- **Daisy-chained configuration**: Connect panels in series (output of one → input of next)
- **Raspberry Pi** with GPIO pins configured for LED matrix control

### Connection Order
1. First panel (Front Face)
2. Second panel (Right Face)  
3. Third panel (Top Face)

## Building

```bash
cd /home/hunter/rgb-matrix/rpi-rgb-led-matrix/examples-api-use
make cube-demo
```

## Running

Basic usage:
```bash
sudo ./cube-demo -D <demo-number> [options]
```

### Demo Numbers

- **0** - Rotating rainbow cube: Flowing rainbow colors with wave animations
- **1** - Animated gradient waves: Smooth waves of color flowing across all faces
- **2** - Pulsing cube: 3D depth effect with pulsing color intensity
- **3** - Cube wireframe: Grid pattern with rotating colors
- **4** - Flowing gradient: Unified gradient flowing across all cube faces

### Common Options

```bash
--led-rows=<rows>        # Panel height (32 or 16, default: 32)
--led-cols=<cols>        # Panel width (32 or 64, default: 32)
--led-gpio-mapping=<map> # GPIO mapping: "regular", "adafruit-hat", etc.
--led-brightness=<0-100> # Brightness percentage
--led-slowdown-gpio=0    # Try if flickering occurs (0-4)
```

## Examples

### Run rainbow demo on standard 32x32 panels
```bash
sudo ./cube-demo -D 0
```

### Run gradient waves with reduced brightness
```bash
sudo ./cube-demo -D 1 --led-brightness=50
```

### Run on 16x32 panels with Adafruit HAT
```bash
sudo ./cube-demo -D 2 --led-rows=16 --led-cols=32 --led-gpio-mapping=adafruit-hat
```

Stop any demo with `Ctrl+C`.

## Display Layout

The cube mapper creates a virtual 2D canvas with 1.5x aspect ratio:

```
Virtual Canvas:
┌─────────────────┐
│  [0,0]  │ [W,0] │  ← Front face | Right face
│─────────┼───────│
│[0,H]    │       │  ← Top face
└─────────────────┘

W = Panel width
H = Panel height
```

Each physical panel displays one cube face seamlessly synchronized with the others.

## How It Works

### Cube Pixel Mapper

The custom `CubePixelMapper` class:
1. Takes a virtual 2D coordinate (x, y) from the drawing functions
2. Determines which cube face it belongs to (front, right, or top)
3. Maps it to the correct physical panel coordinates
4. Sends the pixel to the appropriate LED panel

This happens in real-time as pixels are drawn, creating the illusion of a unified 3D display.

### Animation Sync

All three panels are driven by the same timing and animation functions, ensuring:
- Color transitions flow smoothly across face boundaries
- Animations stay perfectly synchronized
- The cube appears as a single coherent object

## Creating Custom Animations

To add your own animation, add a new function:

```cpp
void DemoYourAnimation(Canvas *canvas) {
  int width = canvas->width();
  int height = canvas->height();
  
  while (!interrupt_received) {
    // Clear and draw your animation
    canvas->Clear();
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        canvas->SetPixel(x, y, r, g, b);
      }
    }
    usleep(30 * 1000);  // ~30ms per frame
  }
}
```

Then add your function to the switch statement in `main()`.

## Performance Tips

- Use consistent frame times (30ms = ~33 FPS is good)
- Draw patterns efficiently to reduce CPU usage
- For mathematical colors, use `HSVToRGB()` instead of computing RGB directly
- If animations appear jerky, increase usleep time or reduce visual complexity

## Troubleshooting

### Panels show random colors or don't respond
- Verify GPIO mapping with `--led-gpio-mapping`
- Try `--led-slowdown-gpio=1` or higher
- Check physical connections between panels

### Animation is flickering
- Increase `--led-pwm-bits` (default: 11)
- Reduce animation complexity
- Try `--led-limit-refresh=30` to cap refresh rate

### Out of memory or crashes
- Simplify animation logic
- Reduce color calculations per frame
- Check for infinite loops (watch for no Ctrl+C response)

## API Reference

Key methods in Canvas class:
- `SetPixel(x, y, r, g, b)` - Set pixel color (0-255 for each channel)
- `Clear()` - Fill with black
- `Fill(r, g, b)` - Fill entire canvas with color
- `width()` / `height()` - Get canvas dimensions
- `DrawLine()`, `DrawCircle()`, `DrawPolygon()` - Draw shapes

For full documentation, see `../include/canvas.h` and `../include/graphics.h`.
