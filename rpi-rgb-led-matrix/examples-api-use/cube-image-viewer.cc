// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
//
// Cube Image Viewer - Display PPM images across the 3D cube
//
// PPM images are simple to work with and can be created from any image
// using ImageMagick: convert image.jpg -resize 96x64 output.ppm
//
// Virtual canvas is 96x64 (for 32x32 panels), maps to cube faces
//
// This code is public domain

#include "led-matrix.h"
#include "graphics.h"

#include <getopt.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

using namespace rgb_matrix;

volatile bool interrupt_received = false;

static void InterruptHandler(int signo) {
  interrupt_received = true;
}

// Simple PPM file reader
struct PPMImage {
  int width;
  int height;
  uint8_t *data;  // RGB data

  PPMImage() : width(0), height(0), data(NULL) {}

  bool Load(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
      fprintf(stderr, "Cannot open file: %s\n", filename);
      return false;
    }

    char magic[3];
    if (fscanf(f, "%2s", magic) != 1 || magic[0] != 'P' || magic[1] != '6') {
      fprintf(stderr, "Not a valid PPM (P6) file\n");
      fclose(f);
      return false;
    }

    int max_val;
    if (fscanf(f, "%d %d %d", &width, &height, &max_val) != 3) {
      fprintf(stderr, "Invalid PPM header\n");
      fclose(f);
      return false;
    }

    fgetc(f);  // Skip whitespace after header

    // Validate dimensions for cube (should be 2x1.5 ratio or fit)
    printf("PPM Image: %dx%d, max color value: %d\n", width, height, max_val);

    // Allocate buffer
    data = new uint8_t[width * height * 3];
    if (!data) {
      fprintf(stderr, "Memory allocation failed\n");
      fclose(f);
      return false;
    }

    // Read pixel data
    if (fread(data, 1, width * height * 3, f) != (size_t)(width * height * 3)) {
      fprintf(stderr, "Failed to read image data\n");
      delete[] data;
      data = NULL;
      fclose(f);
      return false;
    }

    fclose(f);
    printf("Successfully loaded: %s\n", filename);
    return true;
  }

  void Draw(Canvas *canvas) {
    if (!data) {
      fprintf(stderr, "No image data loaded\n");
      return;
    }

    int canvas_w = canvas->width();
    int canvas_h = canvas->height();

    printf("Drawing %dx%d image to %dx%d canvas\n", width, height, canvas_w, canvas_h);

    for (int y = 0; y < canvas_h && y < height; y++) {
      for (int x = 0; x < canvas_w && x < width; x++) {
        int pixel_idx = (y * width + x) * 3;
        uint8_t r = data[pixel_idx];
        uint8_t g = data[pixel_idx + 1];
        uint8_t b = data[pixel_idx + 2];
        canvas->SetPixel(x, y, r, g, b);
      }
    }
  }

  void DrawTiled(Canvas *canvas, int tile_x, int tile_y) {
    // Draw tiled across cube faces
    if (!data) {
      fprintf(stderr, "No image data loaded\n");
      return;
    }

    int canvas_w = canvas->width();
    int canvas_h = canvas->height();

    for (int y = 0; y < canvas_h; y++) {
      for (int x = 0; x < canvas_w; x++) {
        int src_x = (x + tile_x * width) % width;
        int src_y = (y + tile_y * height) % height;
        int pixel_idx = (src_y * width + src_x) * 3;

        uint8_t r = data[pixel_idx];
        uint8_t g = data[pixel_idx + 1];
        uint8_t b = data[pixel_idx + 2];
        canvas->SetPixel(x, y, r, g, b);
      }
    }
  }

  void DrawWithEffect(Canvas *canvas, int effect_type, float time) {
    if (!data) return;

    int canvas_w = canvas->width();
    int canvas_h = canvas->height();

    switch (effect_type) {
    case 0:  // Fade in/out effect
    {
      float alpha = 0.5 + 0.5 * sin(time * 2);
      for (int y = 0; y < canvas_h && y < height; y++) {
        for (int x = 0; x < canvas_w && x < width; x++) {
          int pixel_idx = (y * width + x) * 3;
          uint8_t r = (uint8_t)(data[pixel_idx] * alpha);
          uint8_t g = (uint8_t)(data[pixel_idx + 1] * alpha);
          uint8_t b = (uint8_t)(data[pixel_idx + 2] * alpha);
          canvas->SetPixel(x, y, r, g, b);
        }
      }
    }
    break;

    case 1:  // Scroll effect
    {
      int scroll_x = (int)(time * 10) % width;
      DrawTiled(canvas, scroll_x, 0);
    }
    break;

    case 2:  // Pulse with animation
    {
      float scale = 0.7 + 0.3 * sin(time);
      int offset = (int)((1.0 - scale) * width / 2);

      for (int y = 0; y < canvas_h; y++) {
        for (int x = 0; x < canvas_w; x++) {
          int src_x = (int)((x - offset) / scale);
          int src_y = (int)(y / scale);

          if (src_x >= 0 && src_x < width && src_y >= 0 && src_y < height) {
            int pixel_idx = (src_y * width + src_x) * 3;
            uint8_t r = data[pixel_idx];
            uint8_t g = data[pixel_idx + 1];
            uint8_t b = data[pixel_idx + 2];
            canvas->SetPixel(x, y, r, g, b);
          } else {
            // Background in unused areas
            canvas->SetPixel(x, y, 0, 0, 0);
          }
        }
      }
    }
    break;

    default:
      Draw(canvas);
    }
  }

  ~PPMImage() {
    if (data) delete[] data;
  }
};

static int usage(const char *progname) {
  fprintf(stderr, "usage: %s [options] <image.ppm>\n", progname);
  fprintf(stderr, "\nOptions:\n");
  fprintf(stderr, "\t-e <effect>   : Effect type (0=static, 1=fade, 2=scroll, 3=pulse)\n");
  fprintf(stderr, "\t-t <seconds>  : Animation duration (0=infinite)\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Example - create PPM from image:\n");
  fprintf(stderr, "  convert photo.jpg -resize 96x64 photo.ppm\n");
  fprintf(stderr, "  sudo %s -e 0 photo.ppm\n\n", progname);
  rgb_matrix::PrintMatrixFlags(stderr);
  return 1;
}

int main(int argc, char *argv[]) {
  RGBMatrix::Options matrix_options;
  rgb_matrix::RuntimeOptions runtime_opt;

  matrix_options.rows = 32;
  matrix_options.cols = 32;
  matrix_options.chain_length = 3;
  matrix_options.parallel = 1;

  int effect_type = 0;
  int duration = 0;

  int opt;
  while ((opt = getopt(argc, argv, "e:t:")) != -1) {
    switch (opt) {
    case 'e':
      effect_type = atoi(optarg);
      break;
    case 't':
      duration = atoi(optarg);
      break;
    default:
      return usage(argv[0]);
    }
  }

  if (!rgb_matrix::ParseOptionsFromFlags(&argc, &argv,
                                         &matrix_options, &runtime_opt)) {
    return usage(argv[0]);
  }

  if (optind >= argc) {
    fprintf(stderr, "Missing image filename\n");
    return usage(argv[0]);
  }

  const char *image_file = argv[optind];

  // Load image
  PPMImage image;
  if (!image.Load(image_file)) {
    return 1;
  }

  Canvas *canvas = RGBMatrix::CreateFromFlags(&argc, &argv, &matrix_options);
  if (canvas == NULL) {
    fprintf(stderr, "Failed to create matrix\n");
    return 1;
  }

  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);

  printf("Canvas: %dx%d\n", canvas->width(), canvas->height());
  printf("Effect: %d, Duration: %d seconds\n", effect_type, duration);
  printf("Press Ctrl+C to stop\n");

  float anim_time = 0;
  time_t start_time = time(NULL);

  // Display loop
  while (!interrupt_received) {
    canvas->Clear();

    if (effect_type == 0) {
      // Static display
      image.Draw(canvas);
    } else {
      // Animated effect
      image.DrawWithEffect(canvas, effect_type - 1, anim_time);
    }

    usleep(50 * 1000);  // 20 FPS for image display
    anim_time += 0.05;

    if (duration > 0 && (time(NULL) - start_time) > duration) {
      break;
    }
  }

  canvas->Clear();
  delete canvas;

  printf("Image viewer finished\n");
  return 0;
}
