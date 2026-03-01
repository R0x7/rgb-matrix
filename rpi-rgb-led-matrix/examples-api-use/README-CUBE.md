# 3D LED Cube Display System

Transform three LED display panels into a unified 3D cube visualization system.

## 🎲 What's Included

Three complete applications for displaying cohesive images on a 3-panel cube:

### 1. **cube-demo** - Fast Real-Time Animations
Pre-built animations optimized for the cube display.

```bash
sudo ./cube-demo -D <0-4>
```

- **Demo 0**: Rainbow rotations with wave patterns
- **Demo 1**: Flowing color gradients
- **Demo 2**: 3D pulsing effect with depth
- **Demo 3**: Grid wireframe with rotation
- **Demo 4**: Continuous gradient flow

### 2. **cube-enhanced** - Advanced Visual Effects
Complex mathematical patterns and 3D rendering.

```bash
sudo ./cube-enhanced -D <0-2>
```

- **Demo 0**: 3D rotating cube wireframe with colored edges
- **Demo 1**: Cellular automaton effects spreading across faces
- **Demo 2**: Kaleidoscopic patterns with mathematical precision

### 3. **cube-image-viewer** - Static & Animated Images
Display PPM images with optional effects.

```bash
sudo ./cube-image-viewer [options] image.ppm
```

- Static display
- Fade in/out
- Scrolling
- Pulsing with zoom

## ⚙️ Installation

### Build Everything

```bash
cd ~/rgb-matrix/rpi-rgb-led-matrix/examples-api-use
make all
```

### Build Only Cube Apps

```bash
make cube-demo cube-enhanced cube-image-viewer
```

## 🚀 Quick Start

### Run a Demo

```bash
sudo ./cube-demo -D 0
```

Press `Ctrl+C` to stop.

### With Options

```bash
# 50% brightness
sudo ./cube-demo -D 1 --led-brightness=50

# Different panel size (16x32)
sudo ./cube-demo -D 2 --led-rows=16 --led-cols=32

# Adafruit HAT GPIO mapping
sudo ./cube-demo -D 3 --led-gpio-mapping=adafruit-hat

# Limit refresh rate
sudo ./cube-demo -D 4 --led-limit-refresh=25
```

## 📐 Physical Setup

### Panel Arrangement

Connect three 32x32 RGB LED panels in a daisy chain, arranged to form a cube:

```
┌─────────────────┐
│   Panel 3       │  (Top Face)
│   (Top)         │
├────────┬────────┤
│ Panel 1│ Panel 2│  (Front | Right)
│(Front) │(Right) │
└────────┴────────┘
```

### Connection Order

1. **First panel** → Front face (displays left side)
2. **Second panel** → Right face (displays right side)  
3. **Third panel** → Top face (displays bottom content)

### Wiring

- Connect GPIO pins to Panel 1
- Panel 1 Out → Panel 2 In (daisy-chain)
- Panel 2 Out → Panel 3 In (daisy-chain)

See `../../wiring.md` for detailed GPIO connections.

## 🎨 Virtual Canvas Mapping

The system creates an automatic virtual-to-physical mapping:

```
Virtual Canvas (what you draw)     Physical Panels (what appears)
┌────────────────────────┐
│ Front  │  Right        │    Panel 1: Front (0,0)    Panel 2: Right (W,0)
│(0,0)─ (W,H)             │
├────────────────────┼   Panel 3: Top (0,H)
│ Top    │(unused)       │
│(0,H)  (W,1.5H)        │
└────────────────────────┘

Where:
W = Panel Width (32 or 64)
H = Panel Height (32 or 16)
```

**Key Property**: Patterns automatically flow seamlessly across all three faces!

## 📸 Image Display

### Convert Images to PPM

Use ImageMagick to prepare images:

```bash
# Resize to fit the cube canvas (2x1.5 ratio)
convert photo.jpg -resize 96x64 photo.ppm

# For different panel sizes
convert photo.jpg -resize 128x96 photo.ppm    # For 64x32 panels
```

### Display Static Image

```bash
sudo ./cube-image-viewer photo.ppm
```

### Display with Animation

```bash
# Fade effect
sudo ./cube-image-viewer -e 1 photo.ppm

# Scrolling effect
sudo ./cube-image-viewer -e 2 photo.ppm

# Pulse/zoom effect
sudo ./cube-image-viewer -e 3 photo.ppm

# Auto-stop after 30 seconds
sudo ./cube-image-viewer -e 0 -t 30 photo.ppm
```

## 🛠️ Creating Custom Animations

### Modify Existing Demo

Edit `cube-demo.cc`, change parameters in a demo function:

```cpp
void DemoRotatingRainbow(Canvas *canvas) {
  // ... existing code ...
  float time_increment = 0.05;  // Change animation speed
  uint8_t base_saturation = 200;  // Change color saturation
  // ...
}
```

Rebuild:
```bash
make clean && make cube-demo
sudo ./cube-demo -D 0
```

### Add New Animation

Add a function to `cube-demo.cc`:

```cpp
void DemoMyEffect(Canvas *canvas) {
  int width = canvas->width();
  int height = canvas->height();
  int frame = 0;
  
  while (!interrupt_received) {
    canvas->Clear();
    frame++;
    
    // Your drawing code here
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        // Calculate color based on x, y, frame
        float hue = (x + y + frame) % 360;
        uint8_t r, g, b;
        HSVToRGB(hue, 255, 128, &r, &g, &b);
        canvas->SetPixel(x, y, r, g, b);
      }
    }
    
    usleep(30 * 1000);  // 30ms = ~33 FPS
  }
}
```

Add to `main()` switch statement:

```cpp
case 5:
    DemoMyEffect(canvas);
    break;
```

Recompile:
```bash
make cube-demo
sudo ./cube-demo -D 5
```

## 🎯 API Reference

### Canvas Methods

```cpp
// Pixel operations
canvas->SetPixel(x, y, r, g, b);
canvas->Clear();                         // Fill black
canvas->Fill(r, g, b);                   // Fill with color

// Shape drawing
canvas->DrawLine(x1, y1, x2, y2, r, g, b);
canvas->DrawCircle(cx, cy, radius, r, g, b);
canvas->DrawFilledCircle(cx, cy, radius, r, g, b);

// Information
int w = canvas->width();
int h = canvas->height();

// Color conversion
uint8_t r, g, b;
HSVToRGB(hue, saturation, value, &r, &g, &b);
```

### Coordinate System

```cpp
// Virtual canvas coordinates (what you use)
canvas->SetPixel(x, y, r, g, b);

// x ranges: [0, canvas->width())
// y ranges: [0, canvas->height())

// Automatically mapped to:
// - Front face: x ∈ [0, W/2), y ∈ [0, H*1.5)
// - Right face: x ∈ [W/2, W), y ∈ [0, H*1.5)
// - Top face: x ∈ [0, W/2), y ∈ [H*1.5, H)
```

## ⚡ Performance Tips

### Optimization Strategies

```cpp
// 1. Pre-calculate expensive operations
float t_sin = sin(time * 0.01);
for (y...) for (x...) {
  color = base_color * t_sin;  // Use cached value
}

// 2. Use lookup tables
uint8_t sine_lut[256];  // Pre-computed sine values
color = sine_lut[index];

// 3. Integer math when possible
int intensity = (x * y) >> 8;  // Faster than /

// 4. Minimize function calls in loops
uint8_t r, g, b;
for (y...) for (x...) {
  my_color_func(x, y, r, g, b);  // Better than inlining

// 5. Adjust frame timing
usleep(50 * 1000);  // Slower ~= more time for computation
```

### Frame Rate Guidelines

- **20-30ms (~33-50 FPS)**: Smooth animations, low CPU
- **30-50ms (~20-33 FPS)**: Complex effects, moderate CPU
- **50-100ms (~10-20 FPS)**: Heavy calculations, still smooth

## 🐛 Troubleshooting

### Nothing displays on any panel

```bash
# Check GPIO mapping
sudo ./cube-demo -D 0 --led-gpio-mapping=regular

# Verify physical connections
# Power each panel individually

# Try different mapping if using adapter
sudo ./cube-demo -D 0 --led-gpio-mapping=adafruit-hat
```

### Flickering or noisy display

```bash
# Increase color depth
sudo ./cube-demo -D 0 --led-pwm-bits=13

# Slow down GPIO
sudo ./cube-demo -D 0 --led-slowdown-gpio=2

# Limit refresh rate
sudo ./cube-demo -D 0 --led-limit-refresh=25

# Check power supply (may need beefier supply)
```

### Animation is slow/jerky

```bash
# Increase sleep time
# Edit cube-demo.cc, change usleep(30 * 1000) to usleep(50 * 1000)

# Simplify drawing code
# Remove expensive calculations

# Use --led-limit-refresh to cap refresh rate
```

### One or two panels don't show content

```bash
# Check daisy-chain connection order
# Verify cables between panels

# Test individually:
# Chain only 1 panel, run demo
# Chain 1+2, check if #2 works
# Chain 1+2+3, check if #3 works
```

### Crashes or freezes

```bash
# Check array bounds in loops
if (x >= 0 && x < canvas->width() && y >= 0 && y < canvas->height())
  canvas->SetPixel(x, y, r, g, b);

# Ensure interrupt handler is being used
while (!interrupt_received) { ... }

# Kill stuck process
killall cube-demo
```

## 📚 Documentation

- [Detailed Setup Guide](CUBE-SETUP-GUIDE.md) - In-depth configuration
- [Demo README](CUBE-DEMO-README.md) - Original demo documentation
- Main library: `../../README.md`
- Canvas API: `../../include/canvas.h`
- Graphics utilities: `../../include/graphics.h`

## 📁 Files Included

```
examples-api-use/
├── cube-demo.cc              # 5 basic animation demos
├── cube-enhanced.cc          # 3 advanced effect demos  
├── cube-image-viewer.cc      # PPM image display utility
├── CUBE-DEMO-README.md       # Feature documentation
├── CUBE-SETUP-GUIDE.md       # Detailed setup guide
├── README (this file)
├── Makefile                  # Build configuration
└── [other example files...]
```

## 📝 Examples

### Run the rainbow demo
```bash
cd ~/rgb-matrix/rpi-rgb-led-matrix/examples-api-use
make cube-demo
sudo ./cube-demo -D 0
```

### Create and display an image
```bash
# Convert image
convert my-photo.jpg -resize 96x64 cube-image.ppm

# Display it
sudo ./cube-image-viewer cube-image.ppm

# With fade effect
sudo ./cube-image-viewer -e 1 cube-image.ppm
```

### Run advanced effects
```bash
make cube-enhanced
sudo ./cube-enhanced -D 0  # 3D cube
sudo ./cube-enhanced -D 2  # Kaleidoscope
```

## 🚀 Next Steps

1. ✅ Build all cube apps: `make all`
2. ✅ Run a demo: `sudo ./cube-demo -D 0`
3. ✅ Adjust brightness/colors by editing code
4. ✅ Create your own animation function
5. ✅ Load images with cube-image-viewer
6. ✅ Experiment with different effects

## 💡 Tips for Best Results

- Use high-quality power supply (3A+ for 3 panels)
- Keep GPIO cable runs short and shielded
- Test each component separately first
- Start with simple patterns before complex math
- Monitor CPU temperature during long animations
- Use `--led-limit-refresh=25` for stable operation

---

**Happy cube displaying! 🎨🎲**

Questions? See the detailed guides in this directory or the main library documentation.
