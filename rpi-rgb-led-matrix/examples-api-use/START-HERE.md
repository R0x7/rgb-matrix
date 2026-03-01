# 🎲 LED Cube Display System - Getting Started

## What You Now Have

Three production-ready applications to display cohesive images across three LED panels arranged as a 3D cube:

### The Three Programs

| Program | Purpose | Use Case |
|---------|---------|----------|
| **cube-demo** | 5 fast, smooth animations | Real-time visual displays |
| **cube-enhanced** | 3 complex mathematical effects | Advanced visualizations |
| **cube-image-viewer** | Display PPM images with effects | Static images, photos |

## 🚀 30-Second Quick Start

```bash
cd ~/rgb-matrix/rpi-rgb-led-matrix/examples-api-use

# Build
make all

# Run
sudo ./cube-demo -D 0

# Stop with Ctrl+C
```

That's it! You should see animations on all three panels flowing together as one image.

## 📖 Learn More

### For the Impatient
- Start with: `README-CUBE.md` (main guide, 5 min read)
- Try demos: Run each `cube-demo -D 0-4` and `cube-enhanced -D 0-2`
- Load images: `sudo ./cube-image-viewer photo.ppm`

### For the Detail-Oriented
- **CUBE-SETUP-GUIDE.md** - Comprehensive setup, customization, troubleshooting
- **CUBE-DEMO-README.md** - Detailed feature descriptions and API reference

### For Developers
- Modify `cube-demo.cc` to create custom animations
- See "Creating Custom Animations" in CUBE-SETUP-GUIDE.md
- Check `../../include/canvas.h` for full API

## 🎨 Three Cool Things You Can Do

### 1. Display a Photo
```bash
convert photo.jpg -resize 96x64 photo.ppm
sudo ./cube-image-viewer photo.ppm
```

### 2. Run an Animation
```bash
sudo ./cube-demo -D 0       # Rainbow
sudo ./cube-demo -D 2       # Pulsing
sudo ./cube-enhanced -D 0   # 3D cube
```

### 3. Edit & Create
```bash
# Edit animation speed/colors
vim cube-demo.cc

# Rebuild
make cube-demo

# Run your version
sudo ./cube-demo -D 0
```

## 🔧 Common Commands

```bash
# Build everything
make all

# Build just one program
make cube-demo
make cube-enhanced
make cube-image-viewer

# Clean up compiled files
make clean

# Run demo with options
sudo ./cube-demo -D 0 --led-brightness=50
sudo ./cube-demo -D 1 --led-rows=16 --led-cols=32

# View help
./cube-image-viewer --help
```

## 📊 Architecture

### How It Works

```
Your Code (e.g., cube-demo.cc)
           ↓
    Draw to Virtual Canvas
    (256×192 or 96×64)
           ↓
    Automatic Mapping
    ├─→ Front panel gets (0,0) to (W,H)
    ├─→ Right panel gets (W,0) to (2W,H)
    └─→ Top panel gets (0,H) to (W,1.5H)
           ↓
    Physical LED Panels
    ┌─────────────────┐
    │ [Top]           │
    ├──────┬──────────┤
    │[Front] [Right]  │
    └──────┴──────────┘
```

### Key Magic: The Pixel Mapper

The system automatically maps virtual coordinates to the correct physical panel. When you draw at position (x, y), it:
1. Determines which cube face it belongs to
2. Remaps the coordinates for that panel
3. Sends the pixel to the right hardware

This happens transparently—you just draw normally!

## 📝 File Guide

| File | Purpose | Length |
|------|---------|--------|
| `cube-demo.cc` | 5 animation demos | 400 lines |
| `cube-enhanced.cc` | 3 advanced effects | 350 lines |
| `cube-image-viewer.cc` | Image display utility | 300 lines |
| `README-CUBE.md` | Main documentation | Complete guide |
| `CUBE-SETUP-GUIDE.md` | Detailed reference | Comprehensive |
| `CUBE-DEMO-README.md` | Feature overview | Technical details |

## 🎯 Your Next Step

**Pick one:**

1️⃣ **Just want to see it work?**
   - Run: `sudo ./cube-demo -D 0`
   - Read: `README-CUBE.md` (5 min)

2️⃣ **Want to customize colors/speed?**
   - Edit: `cube-demo.cc` (find floating-point constants)
   - Rebuild: `make cube-demo`
   - Run: `sudo ./cube-demo -D 0`

3️⃣ **Want to display your own image?**
   - Convert: `convert photo.jpg -resize 96x64 photo.ppm`
   - Run: `sudo ./cube-image-viewer photo.ppm`

4️⃣ **Want to create a new animation?**
   - Read: "Creating Custom Animations" in CUBE-SETUP-GUIDE.md
   - Add function to: `cube-demo.cc`
   - Build and test

5️⃣ **Want to understand everything?**
   - Read: CUBE-SETUP-GUIDE.md (complete reference)
   - Study: Source code comments in the .cc files
   - Experiment: Modify and rebuild

## ❓ Quick Q&A

**Q: Do I need to modify the files?**
A: Not at all! All three programs work as-is. Modification is optional.

**Q: Can I run animations at the same time?**
A: One at a time, but they run at your chosen refresh rate. The demos at 30Hz are smooth.

**Q: Do I need the GPU?**
A: No, this is CPU-based. Works on any Raspberry Pi with GPIO.

**Q: Can I use different panel sizes?**
A: Yes! Use `--led-rows` and `--led-cols` flags.

**Q: What if a panel doesn't work?**
A: See "Troubleshooting" in CUBE-SETUP-GUIDE.md for step-by-step diagnosis.

## 🎓 Learning Path

```
Beginner: Run demos → Adjust brightness → Read README-CUBE.md
    ↓
Intermediate: Create custom animation function
    ↓  
Advanced: Create complex mathematical effects
    ↓
Expert: Optimize for performance, add new features
```

## 📚 Where to Find Help

| Question | Answer | Location |
|----------|--------|----------|
| "How do I use this?" | See quick start above | ↑ |
| "What's the API?" | Canvas methods | CUBE-SETUP-GUIDE.md |
| "How do I troubleshoot?" | Step-by-step guide | CUBE-SETUP-GUIDE.md |
| "Create animations?" | Code templates | CUBE-SETUP-GUIDE.md |
| "Performance tips?" | Optimization advice | CUBE-SETUP-GUIDE.md |
| "Full documentation?" | Comprehensive guide | README-CUBE.md |
| "Library reference?" | Canvas API | ../../include/canvas.h |

## ⚡ Pro Tips

1. **Ctrl+C** stops any program cleanly
2. **--led-brightness=50** reduces power draw and heat
3. **--led-limit-refresh=25** stabilizes flickering displays
4. **HSVToRGB()** is easier than RGB for animations
5. Use **lookup tables** for expensive calculations
6. **Pre-calculate** values before loops

## 🎯 You're Ready!

Everything is built and ready to use. Just run:

```bash
sudo ./cube-demo -D 0
```

See anything? You're good to go! 🎉

Want to go deeper? Pick a documentation file above and dive in.

---

**Questions?** Check the troubleshooting section in CUBE-SETUP-GUIDE.md

**Errors?** Make sure:
- sudo is used (hardware access)
- All three panels are daisy-chained
- GPIO pins match your setup

**Customizing?** Start by reading CUBE-SETUP-GUIDE.md "Creating Custom Animations" section.

---

**Enjoy your LED cube!** 🎨🎲✨
