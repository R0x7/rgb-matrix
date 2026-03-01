# LED Cube Display System - Complete Guide

## What You Now Have

Your `examples-api-use` directory now contains two complete cube display applications:

### 1. **cube-demo** - Basic Animation Suite
Simple, fast animations optimized for smooth real-time rendering across 3 faces.

**Available demos:**
- 0: Rainbow rotations with waves
- 1: Color gradient animations  
- 2: 3D pulsing effect
- 3: Grid wireframe patterns
- 4: Flowing gradients

### 2. **cube-enhanced** - Advanced Effects
More complex visual effects with mathematical patterns and 3D rendering.

**Available demos:**
- 0: 3D rotating cube wireframe
- 1: Cellular automaton effect
- 2: Kaleidoscopic patterns

## Quick Start

### Building

```bash
cd ~/rgb-matrix/rpi-rgb-led-matrix/examples-api-use
make all
```

This compiles both `cube-demo` and `cube-enhanced` binaries.

### Running

```bash
sudo ./cube-demo -D 0
```

Press `Ctrl+C` to stop.

### With Options

```bash
# 16x32 panels with Adafruit HAT
sudo ./cube-demo -D 1 --led-rows=16 --led-cols=32 --led-gpio-mapping=adafruit-hat

# 32x32 panels at 50% brightness
sudo ./cube-demo -D 2 --led-brightness=50

# Enhanced demo on standard setup
sudo ./cube-enhanced -D 0
```

## Physical Setup

Connect your three 32x32 LED panels in this order:

```
[Panel 1: Front Face]
        ↓ (daisy-chain)
[Panel 2: Right Face]
        ↓ (daisy-chain)
[Panel 3: Top Face]
```

The software automatically maps content to the correct face.

## Under the Hood

### Virtual Canvas Layout

The system creates a virtual 2×1.5 aspect ratio canvas that automatically maps to your three physical panels:

```
Original Virtual Canvas (used for drawing)
┌────────────────────────┐
│    Front    │    Right   │
│  [0,0] (W,0)│           │
├────────────────────────┤
│    Top      │(unused)    │
│  [0,H]      │           │
└────────────────────────┘

W = Panel Width
H = Panel Height

This maps to:
Panel 1: Front (0,0) → (W,H)
Panel 2: Right (W,0) → (2W,H)
Panel 3: Top   (0,H) → (W,1.5H)
```

### Coordinate System

When you draw at position `(x, y)` on the virtual canvas:

```cpp
canvas->SetPixel(x, y, r, g, b);
```

The mapping happens automatically:
- **Front face**: Use x ∈ [0, width/2), y ∈ [0, height/1.5)
- **Right face**: Use x ∈ [width/2, width), y ∈ [0, height/1.5)
- **Top face**: Use x ∈ [0, width/2), y ∈ [height/1.5, height)

The unused area (bottom-right) doesn't appear on any physical panel.

## Creating Your Own Animations

### Basic Template

```cpp
void DemoMyAnimation(Canvas *canvas) {
  int width = canvas->width();
  int height = canvas->height();
  
  int frame = 0;
  while (!interrupt_received) {
    canvas->Clear();
    frame++;
    
    // Your drawing code here
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        uint8_t r, g, b;
        // Calculate colors based on x, y, frame
        canvas->SetPixel(x, y, r, g, b);
      }
    }
    
    usleep(30 * 1000);  // 30ms = ~33 FPS
  }
}
```

Add to the switch statement in `main()`:
```cpp
case 5:
    DemoMyAnimation(canvas);
    break;
```

### Graphics Utilities

The library provides several drawing utilities in `graphics.h`:

```cpp
// Draw lines
canvas->DrawLine(x1, y1, x2, y2, r, g, b);

// Draw circles
canvas->DrawCircle(center_x, center_y, radius, r, g, b);

// Draw filled circles
canvas->DrawFilledCircle(center_x, center_y, radius, r, g, b);

// Fill entire canvas
canvas->Fill(r, g, b);

// HSV to RGB conversion
uint8_t r, g, b;
HSVToRGB(hue, saturation, value, &r, &g, &b);
```

### Performance Tips

1. **Minimize per-pixel calculations**
   ```cpp
   // Good: Precalculate
   float time_sin = sin(frame * 0.01);
   for (y = 0; y < height; y++) {
     for (x = 0; x < width; x++) {
       color = base_color * time_sin;
   ```

2. **Use integer math when possible**
   ```cpp
   // Faster than floating point
   int intensity = (x * y) >> 8;
   ```

3. **Avoid expensive functions in inner loops**
   ```cpp
   // Bad:
   for (y...) for (x...) 
     color = sin(atan2(y, x));
   
   // Good:
   for (y...) for (x...) 
     color = lookup_table[angle_index];
   ```

4. **Frame rate optimization**
   - 30-50ms sleep = 20-33 FPS (smooth for animations)
   - Increase sleep if CPU usage is high
   - Decrease for faster effects

## Advanced: Loading Images

For displaying static images or parts of images on the cube:

### Using ImageMagick (like image-example.cc)

```cpp
#include <Magick++.h>

Magick::Image img("myimage.png");
img.resize(Geometry(canvas->width(), canvas->height()));

for (int y = 0; y < canvas->height(); y++) {
  for (int x = 0; x < canvas->width(); x++) {
    const Magick::Color &c = img.pixelColor(x, y);
    canvas->SetPixel(x, y, c.redQuantum() >> 8, 
                           c.greenQuantum() >> 8, 
                           c.blueQuantum() >> 8);
  }
}
```

### Simple PPM Image Display

PPM files are simple text-based image formats:

```cpp
// Minimal PPM loader for raw RGB data
void DrawPPM(Canvas *canvas, const char *filename) {
  FILE *f = fopen(filename, "rb");
  char magic[3];
  int w, h, max_val;
  
  fscanf(f, "%2s %d %d %d", magic, &w, &h, &max_val);
  fgetc(f);  // Skip whitespace
  
  for (int y = 0; y < h && y < canvas->height(); y++) {
    for (int x = 0; x < w && x < canvas->width(); x++) {
      uint8_t r = fgetc(f);
      uint8_t g = fgetc(f);
      uint8_t b = fgetc(f);
      canvas->SetPixel(x, y, r, g, b);
    }
  }
  
  fclose(f);
}
```

## Multi-Canvas Strategy

If you want to manage each face separately, you can create custom logic:

```cpp
// Get references to each face's coordinates
struct Face {
  int x_start, y_start;
  int x_end, y_end;
};

Face front_face = {0, 0, width/2, height*2/3};
Face right_face = {width/2, 0, width, height*2/3};
Face top_face = {0, height*2/3, width/2, height};

// Draw to specific face
void DrawOnFace(Canvas *canvas, const Face &face, uint8_t r, uint8_t g, uint8_t b) {
  for (int y = face.y_start; y < face.y_end; y++) {
    for (int x = face.x_start; x < face.x_end; x++) {
      canvas->SetPixel(x, y, r, g, b);
    }
  }
}
```

## Troubleshooting

### "Nothing appears on panels"
1. Verify GPIO mapping: `--led-gpio-mapping=regular` (default)
2. Check physical connections
3. Try: `sudo ./cube-demo -D 0 --led-gpio-mapping=adafruit-hat`

### "Panels are flickering"
1. Increase PWM bits: `--led-pwm-bits=13`
2. Reduce animation complexity
3. Try different slowdown: `--led-slowdown-gpio=2`

### "Animation is slow or jumpy"
1. Simplify drawing code
2. Use less complex math
3. Increase `usleep()` time in animation loop
4. Try: `--led-limit-refresh=25` to cap refresh rate

### "Only shows garbage or partial image"
1. Verify chain_length=3 in code or flags
2. Check daisy-chain connections between panels
3. Try resetting with: `killall cube-demo` then power-cycle panels

### "Crashes or segfaults"
1. Check array bounds in loops
2. Ensure `interrupt_received` is checked in loops
3. Avoid dynamic memory allocation in realtime loop

## File Structure

```
examples-api-use/
├── cube-demo.cc          # 5 basic animation demos
├── cube-enhanced.cc      # 3 advanced effect demos
├── CUBE-DEMO-README.md   # This file
├── Makefile              # Builds both cube programs
├── [other example files...]
```

## Next Steps

1. **Build and run basic demos**
   ```bash
   make cube-demo cube-enhanced
   sudo ./cube-demo -D 0
   ```

2. **Experiment with different demos**
   ```bash
   sudo ./cube-demo -D 1
   sudo ./cube-enhanced -D 0
   ```

3. **Modify an existing demo** - Edit `cube-demo.cc`, change colors/speed, rebuild

4. **Create your own animation** - Add a new function to `cube-demo.cc`

5. **Load images** - Use the PPM approach or integrate GraphicsMagick

## Support Resources

- Main library README: `../../README.md`
- Canvas API: `../../include/canvas.h`
- Graphics utilities: `../../include/graphics.h`
- LED Matrix API: `../../include/led-matrix.h`
- Example programs: See other `.cc` files in this directory

---

**Happy LED cubing! 🎲✨**
