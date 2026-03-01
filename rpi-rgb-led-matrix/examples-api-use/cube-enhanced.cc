// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
//
// Enhanced Cube Display with Rotatable 3D Cube Model
//
// This version includes a 3D cube model that can be rotated and displayed
// across the three visible faces.
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
#include <vector>

using namespace rgb_matrix;

volatile bool interrupt_received = false;

static void InterruptHandler(int signo) {
  interrupt_received = true;
}

// HSV to RGB color conversion helper function
static void HSVToRGB(float hue, uint8_t saturation, uint8_t value,
                     uint8_t *r, uint8_t *g, uint8_t *b) {
  float h = fmod(hue, 360.0);
  if (h < 0) h += 360.0;
  float s = saturation / 255.0;
  float v = value / 255.0;
  
  float c = v * s;
  float x = c * (1 - fabs(fmod(h / 60.0, 2) - 1));
  float m = v - c;
  
  float rf = 0, gf = 0, bf = 0;
  
  if (h < 60) {
    rf = c; gf = x; bf = 0;
  } else if (h < 120) {
    rf = x; gf = c; bf = 0;
  } else if (h < 180) {
    rf = 0; gf = c; bf = x;
  } else if (h < 240) {
    rf = 0; gf = x; bf = c;
  } else if (h < 300) {
    rf = x; gf = 0; bf = c;
  } else {
    rf = c; gf = 0; bf = x;
  }
  
  *r = (uint8_t)((rf + m) * 255);
  *g = (uint8_t)((gf + m) * 255);
  *b = (uint8_t)((bf + m) * 255);
}

// 3D point structure
struct Point3D {
  float x, y, z;
  Point3D(float x = 0, float y = 0, float z = 0) : x(x), y(y), z(z) {}

  // Rotate around Z axis
  Point3D rotateZ(float angle) const {
    float rad = angle * M_PI / 180.0;
    float cos_a = cos(rad);
    float sin_a = sin(rad);
    return Point3D(x * cos_a - y * sin_a, x * sin_a + y * cos_a, z);
  }

  // Rotate around Y axis
  Point3D rotateY(float angle) const {
    float rad = angle * M_PI / 180.0;
    float cos_a = cos(rad);
    float sin_a = sin(rad);
    return Point3D(x * cos_a + z * sin_a, y, -x * sin_a + z * cos_a);
  }

  // Rotate around X axis
  Point3D rotateX(float angle) const {
    float rad = angle * M_PI / 180.0;
    float cos_a = cos(rad);
    float sin_a = sin(rad);
    return Point3D(x, y * cos_a - z * sin_a, y * sin_a + z * cos_a);
  }

  // Project onto 2D with perspective
  void project(int &screen_x, int &screen_y, int width, int height, float distance = 3.0) {
    float scale = distance / (z + distance);
    screen_x = (int)(width / 2 + x * scale * width / 4);
    screen_y = (int)(height / 2 + y * scale * height / 4);
  }
};

// Demo with 3D rotating cube visualization
void Demo3DRotatingCube(Canvas *canvas) {
  int width = canvas->width();
  int height = canvas->height();
  int panel_width = width / 2;
  int panel_height = (int)(height / 1.5);

  float rot_x = 0, rot_y = 0, rot_z = 0;

  while (!interrupt_received) {
    canvas->Clear();

    rot_x += 0.5;
    rot_y += 0.7;
    rot_z += 0.3;

    // Define cube vertices
    std::vector<Point3D> vertices = {
        Point3D(-1, -1, -1), Point3D(1, -1, -1), Point3D(1, 1, -1), Point3D(-1, 1, -1),
        Point3D(-1, -1, 1),  Point3D(1, -1, 1),  Point3D(1, 1, 1),  Point3D(-1, 1, 1)
    };

    // Apply rotations
    for (auto &v : vertices) {
      v = v.rotateX(rot_x).rotateY(rot_y).rotateZ(rot_z);
    }

    // Draw cube edges with colors
    struct Edge {
      int a, b;
      uint8_t r, g, b;
    };

    std::vector<Edge> edges = {
        // Bottom face
        {0, 1, 255, 0, 0}, {1, 2, 255, 0, 0}, {2, 3, 255, 0, 0}, {3, 0, 255, 0, 0},
        // Top face
        {4, 5, 0, 255, 0}, {5, 6, 0, 255, 0}, {6, 7, 0, 255, 0}, {7, 4, 0, 255, 0},
        // Vertical edges
        {0, 4, 0, 0, 255}, {1, 5, 0, 0, 255}, {2, 6, 0, 0, 255}, {3, 7, 0, 0, 255}
    };

    // Project and draw edges
    for (const auto &edge : edges) {
      int x1, y1, x2, y2;
      vertices[edge.a].project(x1, y1, width, height);
      vertices[edge.b].project(x2, y2, width, height);

      // Clamp coordinates
      x1 = (x1 < 0) ? 0 : ((x1 >= width) ? width - 1 : x1);
      y1 = (y1 < 0) ? 0 : ((y1 >= height) ? height - 1 : y1);
      x2 = (x2 < 0) ? 0 : ((x2 >= width) ? width - 1 : x2);
      y2 = (y2 < 0) ? 0 : ((y2 >= height) ? height - 1 : y2);

      canvas->DrawLine(x1, y1, x2, y2, edge.r, edge.g, edge.b);
    }

    usleep(30 * 1000);
  }
}

// Demo with cellular automaton effect across cube faces
void DemoCA(Canvas *canvas) {
  int width = canvas->width();
  int height = canvas->height();

  std::vector<std::vector<uint8_t>> grid(height, std::vector<uint8_t>(width, 0));

  // Initialize with random cells
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      if (rand() % 5 == 0) {
        grid[y][x] = 255;
      }
    }
  }

  while (!interrupt_received) {
    canvas->Clear();

    // Draw current state
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        if (grid[y][x] > 0) {
          canvas->SetPixel(x, y, grid[y][x], grid[y][x] / 2, 255);
          grid[y][x] = (grid[y][x] > 10) ? grid[y][x] - 10 : 0;
        }
      }
    }

    // Apply CA rules randomly
    if (rand() % 3 == 0) {
      int x = rand() % width;
      int y = rand() % height;
      grid[y][x] = 200;
    }

    usleep(50 * 1000);
  }
}

// Demo: Kaleidoscopic pattern that flows across all faces
void DemoKaleidoscope(Canvas *canvas) {
  int width = canvas->width();
  int height = canvas->height();
  float time = 0;

  while (!interrupt_received) {
    canvas->Clear();
    time += 0.02;

    int center_x = width / 2;
    int center_y = height / 2;

    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        float dx = x - center_x;
        float dy = y - center_y;
        float dist = sqrt(dx * dx + dy * dy);
        float angle = atan2(dy, dx);

        // Create kaleidoscopic effect
        float pattern = sin(dist * 0.1 + time) * 
                       cos(angle * 6 + time * 0.5) *
                       sin(time + dist * 0.05);
        
        int intensity = (int)(128 + pattern * 127);
        intensity = (intensity < 0) ? 0 : ((intensity > 255) ? 255 : intensity);

        uint8_t r = intensity;
        uint8_t g = (uint8_t)(intensity * sin(time));
        uint8_t b = (uint8_t)(intensity * cos(time));

        canvas->SetPixel(x, y, r, g, b);
      }
    }

    usleep(30 * 1000);
  }
}

int main(int argc, char *argv[]) {
  RGBMatrix::Options matrix_options;
  rgb_matrix::RuntimeOptions runtime_opt;

  matrix_options.rows = 32;
  matrix_options.cols = 32;
  matrix_options.chain_length = 3;
  matrix_options.parallel = 1;

  if (!rgb_matrix::ParseOptionsFromFlags(&argc, &argv,
                                         &matrix_options, &runtime_opt)) {
    fprintf(stderr, "usage: %s -D <demo-nr> [options]\n", argv[0]);
    fprintf(stderr, "\nDemo numbers:\n");
    fprintf(stderr, "  0 - 3D rotating cube wireframe\n");
    fprintf(stderr, "  1 - Cellular automaton effect\n");
    fprintf(stderr, "  2 - Kaleidoscopic patterns\n");
    rgb_matrix::PrintMatrixFlags(stderr);
    return 1;
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

  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);

  printf("Running enhanced cube demo #%d\n", demo_number);
  printf("Canvas size: %dx%d\n", canvas->width(), canvas->height());

  switch (demo_number) {
  case 0:
    Demo3DRotatingCube(canvas);
    break;
  case 1:
    DemoCA(canvas);
    break;
  case 2:
    DemoKaleidoscope(canvas);
    break;
  default:
    fprintf(stderr, "Unknown demo number: %d\n", demo_number);
    canvas->Clear();
    delete canvas;
    return 1;
  }

  canvas->Clear();
  delete canvas;
  return 0;
}
