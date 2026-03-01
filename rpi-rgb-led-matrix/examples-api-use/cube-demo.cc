// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
//
// Cube Display Example
// Display a cohesive image across three LED screens arranged as a 3D cube.
// The three visible faces (front, right, top) are arranged in a 2D layout:
//
//     [Top]
// [Front][Right]
//
// This code is public domain
// (but note, that the led-matrix library this depends on is GPL v2)

#include "led-matrix.h"
#include "graphics.h"

#include <getopt.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

using namespace rgb_matrix;

volatile bool interrupt_received = false;

static void InterruptHandler(int signo) {
  interrupt_received = true;
}

static int usage(const char *progname) {
  fprintf(stderr, "usage: %s [options] <demo-number>\n", progname);
  fprintf(stderr, "\nDemo numbers:\n");
  fprintf(stderr, "  0 - Rotating rainbow cube\n");
  fprintf(stderr, "  1 - Animated gradient waves\n");
  fprintf(stderr, "  2 - Pulsing color with 3D effect\n");
  fprintf(stderr, "  3 - 3D cube wireframe with rotation\n");
  fprintf(stderr, "  4 - Animated gradient across faces\n");
  fprintf(stderr, "\nOptions:\n");
  fprintf(stderr,
          "\t-D <demo-number>  : Demo to run (0-4)\n"
          "\t--led-rows=<rows> : Panel rows. Typically 32. (Default: 32)\n"
          "\t--led-cols=<cols> : Panel columns. Typically 32. (Default: 32)\n");
  rgb_matrix::PrintMatrixFlags(stderr);
  return 1;
}

// Custom pixel mapper for cube arrangement
class CubePixelMapper : public PixelMapper {
public:
  CubePixelMapper() : panel_width_(32), panel_height_(32) {}

  virtual const char *GetName() const { return "Cube"; }

  virtual bool SetParameters(int chain, int parallel, const char *param) {
    // Could add parameters for panel orientation/rotation here
    // For now we keep it simple with just the defaults
    return true;
  }

  virtual bool GetSizeMapping(int matrix_width, int matrix_height,
                              int *visible_width, int *visible_height) const {
    // Virtual canvas is 2 panels wide, 1.5 panels tall
    // This gives us: Front | Right
    //                Top
    *visible_width = matrix_width * 2;
    *visible_height = (int)(matrix_height * 1.5);
    return true;
  }

  virtual void MapVisibleToMatrix(int matrix_width, int matrix_height,
                                  int visible_x, int visible_y,
                                  int *matrix_x, int *matrix_y) const {
    panel_width_ = matrix_width;
    panel_height_ = matrix_height;

    // Determine which face and map accordingly
    if (visible_x < matrix_width && visible_y < matrix_height) {
      // Front face (top-left)
      *matrix_x = visible_x;
      *matrix_y = visible_y;
    } else if (visible_x >= matrix_width && visible_y < matrix_height) {
      // Right face (top-right) - map to second physical panel
      *matrix_x = visible_x - matrix_width;
      *matrix_y = visible_y;
    } else if (visible_x < matrix_width && visible_y >= matrix_height) {
      // Top face (bottom-left) - map to third physical panel
      *matrix_x = visible_x;
      *matrix_y = visible_y - matrix_height;
    } else {
      // Out of bounds, shouldn't happen
      *matrix_x = 0;
      *matrix_y = 0;
    }
  }

private:
  mutable int panel_width_;
  mutable int panel_height_;
};

// Demo 0: Rotating rainbow cube
void DemoRotatingRainbow(Canvas *canvas) {
  int width = canvas->width();
  int height = canvas->height();
  int panel_width = width / 2;
  int panel_height = (int)(height / 1.5);

  float time = 0;
  while (!interrupt_received) {
    canvas->Clear();
    time += 0.02;

    // Draw horizontal waves across all faces
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        float h = (x + y + sin(time + x * 0.01) * 20 + cos(time + y * 0.01) * 20);
        h = fmod(h, 256);

        uint8_t r, g, b;
        HSVToRGB(h, 255, 128, &r, &g, &b);
        canvas->SetPixel(x, y, r, g, b);
      }
    }

    usleep(30 * 1000);
  }
}

// Demo 1: Animated gradient waves
void DemoAnimatedGradient(Canvas *canvas) {
  int width = canvas->width();
  int height = canvas->height();

  float time = 0;
  while (!interrupt_received) {
    canvas->Clear();
    time += 0.01;

    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        // Create flowing waves
        float wave = sin(x * 0.02 + time) * cos(y * 0.02 + time);
        int intensity = (int)(128 + wave * 127);
        intensity = (intensity < 0) ? 0 : ((intensity > 255) ? 255 : intensity);

        // Color changes based on position on cube face
        uint8_t r = (x * 255) / width;
        uint8_t g = (y * 255) / height;
        uint8_t b = intensity;

        canvas->SetPixel(x, y, r, g, b);
      }
    }

    usleep(30 * 1000);
  }
}

// Demo 2: Pulsing color with 3D effect
void DemoPulsingCube(Canvas *canvas) {
  int width = canvas->width();
  int height = canvas->height();
  float pulse = 0;

  while (!interrupt_received) {
    canvas->Clear();
    pulse += 0.05;

    int pulse_val = (int)(128 + sin(pulse) * 100);
    pulse_val = (pulse_val < 0) ? 0 : ((pulse_val > 255) ? 255 : pulse_val);

    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        // Create a 3D effect with depth-based coloring
        float dist_from_center = sqrt(pow(x - width / 2.0, 2) + pow(y - height / 2.0, 2));
        float depth = 1.0 - (dist_from_center / (width / 2.0));
        if (depth < 0) depth = 0;

        uint8_t r = (uint8_t)(pulse_val * depth);
        uint8_t g = (uint8_t)(pulse_val * (1 - depth * 0.5));
        uint8_t b = (uint8_t)(pulse_val * (1 - depth));

        canvas->SetPixel(x, y, r, g, b);
      }
    }

    usleep(30 * 1000);
  }
}

// Demo 3: 3D cube wireframe with rotation
void DemoCubeWireframe(Canvas *canvas) {
  int width = canvas->width();
  int height = canvas->height();
  float angle = 0;

  while (!interrupt_received) {
    canvas->Clear();
    angle += 1.5;

    // Draw grid pattern that appears on each face
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        bool is_line = (x % 8 == 0) || (y % 8 == 0);

        if (is_line) {
          // Create a rotating color effect
          float hue = fmod(angle + (x * y / 255.0), 360.0);
          uint8_t r, g, b;
          HSVToRGB(hue, 255, 200, &r, &g, &b);
          canvas->SetPixel(x, y, r, g, b);
        } else {
          // Dark background
          uint8_t bg = (uint8_t)(64 * (1 + sin(angle * 0.01 + x * 0.01)));
          canvas->SetPixel(x, y, bg / 4, bg / 8, bg / 4);
        }
      }
    }

    usleep(30 * 1000);
  }
}

// Demo 4: Unified gradient that flows across cube faces
void DemoFlowingGradient(Canvas *canvas) {
  int width = canvas->width();
  int height = canvas->height();
  float time = 0;

  while (!interrupt_received) {
    canvas->Clear();
    time += 0.02;

    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        // Create a flowing gradient that appears continuous across faces
        float gradient = fmod((x + y + time * 50), 256);
        uint8_t r = (uint8_t)gradient;
        uint8_t g = (uint8_t)(gradient * 0.7);
        uint8_t b = (uint8_t)(256 - gradient);

        canvas->SetPixel(x, y, r, g, b);
      }
    }

    usleep(30 * 1000);
  }
}

int main(int argc, char *argv[]) {
  RGBMatrix::Options matrix_options;
  rgb_matrix::RuntimeOptions runtime_opt;

  // Set defaults for a cube with 3 panels
  matrix_options.rows = 32;
  matrix_options.cols = 32;
  matrix_options.chain_length = 3;  // 3 panels for the 3 faces
  matrix_options.parallel = 1;

  if (!rgb_matrix::ParseOptionsFromFlags(&argc, &argv,
                                         &matrix_options, &runtime_opt)) {
    return usage(argv[0]);
  }

  int demo_number = 0;
  if (argc > 1) {
    demo_number = atoi(argv[1]);
  }

  Canvas *canvas = RGBMatrix::CreateFromFlags(&argc, &argv, &matrix_options);
  if (canvas == NULL) {
    fprintf(stderr, "Failed to create matrix\n");
    return 1;
  }

  // Register and apply the cube pixel mapper
  CubePixelMapper *cube_mapper = new CubePixelMapper();
  canvas = canvas->CreateFrameCanvas();  // Get the initial canvas
  
  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);

  printf("Running cube demo #%d\n", demo_number);
  printf("Canvas size: %dx%d\n", canvas->width(), canvas->height());

  switch (demo_number) {
  case 0:
    DemoRotatingRainbow(canvas);
    break;
  case 1:
    DemoAnimatedGradient(canvas);
    break;
  case 2:
    DemoPulsingCube(canvas);
    break;
  case 3:
    DemoCubeWireframe(canvas);
    break;
  case 4:
    DemoFlowingGradient(canvas);
    break;
  default:
    fprintf(stderr, "Unknown demo number: %d\n", demo_number);
    return usage(argv[0]);
  }

  canvas->Clear();
  delete canvas;

  printf("Cube demo finished\n");
  return 0;
}
