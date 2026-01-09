#include "draw.h"
#include "undo.h"

void insert_paint(void) {
  if (insert_mode) {
    begin_undo_action();
    paint_brush(cursor_x, cursor_y, fg_color);
    commit_undo_action();
  }
}

void draw_line(int x0, int y0, int x1, int y1, guint32 color) {
  int dx = abs(x1 - x0), dy = abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  int cx = x0, cy = y0;
  while (1) {
    paint_pixel(cx, cy, color);
    if (cx == x1 && cy == y1)
      break;
    int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      cx += sx;
    }
    if (e2 < dx) {
      err += dx;
      cy += sy;
    }
  }
}

void paint_pixel_raw(int x, int y, guint32 color) {
  if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H)
    return;
  push_undo(x, y);
  PX(y, x) = color;
}

void paint_pixel(int x, int y, guint32 color) {
  if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H)
    return;
  paint_pixel_raw(x, y, color);
  switch (sym_mode) {
  case SYM_H: {
    int mx = CANVAS_W - 1 - x;
    if (mx != x)
      paint_pixel_raw(mx, y, color);
    break;
  }
  case SYM_V: {
    int my = CANVAS_H - 1 - y;
    if (my != y)
      paint_pixel_raw(x, my, color);
    break;
  }
  case SYM_HV: {
    int mx = CANVAS_W - 1 - x, my = CANVAS_H - 1 - y;
    if (mx != x)
      paint_pixel_raw(mx, y, color);
    if (my != y)
      paint_pixel_raw(x, my, color);
    if (mx != x && my != y)
      paint_pixel_raw(mx, my, color);
    break;
  }
  case SYM_RADIAL: {
    double cx = (CANVAS_W - 1) / 2.0, cy = (CANVAS_H - 1) / 2.0;
    double rx = x - cx, ry = y - cy;
    double r = sqrt(rx * rx + ry * ry);
    double theta = atan2(ry, rx);
    double step = 2.0 * M_PI / sym_radial_n;
    for (int k = 1; k < sym_radial_n; k++) {
      double a = theta + k * step;
      int px = (int)(cx + r * cos(a) + 0.5);
      int py = (int)(cy + r * sin(a) + 0.5);
      if (px != x || py != y)
        paint_pixel_raw(px, py, color);
    }
    break;
  }
  default:
    break;
  }
}

#define CUSTOM_BRUSH_MAX 16
void paint_brush(int x, int y, guint32 color) {
  if (brush_shape == 2 && custom_brush_w > 0 && custom_brush_h > 0) {
    int ox = custom_brush_w / 2, oy = custom_brush_h / 2;
    for (int dy = 0; dy < custom_brush_h; dy++)
      for (int dx = 0; dx < custom_brush_w; dx++) {
        if (!custom_brush_pixels[dy][dx])
          continue;
        if (spray_density > 0 && (rand() % 100) >= spray_density)
          continue;
        paint_pixel(x - ox + dx, y - oy + dy, color);
      }
    return;
  }
  int half = brush_size / 2;
  for (int dy = 0; dy < brush_size; dy++)
    for (int dx = 0; dx < brush_size; dx++) {
      if (brush_shape == 1) {
        int cdx = dx - half, cdy = dy - half;
        if (cdx * cdx + cdy * cdy > half * half)
          continue;
      }
      if (spray_density > 0 && (rand() % 100) >= spray_density)
        continue;
      paint_pixel(x - half + dx, y - half + dy, color);
    }
}

void find_right(void) {
  for (int fx = cursor_x + 1; fx < CANVAS_W; fx++)
    if (PX(cursor_y, fx)) {
      cursor_x = fx;
      return;
    }
  for (int fx = 0; fx < cursor_x; fx++)
    if (PX(cursor_y, fx)) {
      cursor_x = fx;
      return;
    }
}
void find_left(void) {
  for (int fx = cursor_x - 1; fx >= 0; fx--)
    if (PX(cursor_y, fx)) {
      cursor_x = fx;
      return;
    }
  for (int fx = CANVAS_W - 1; fx > cursor_x; fx--)
    if (PX(cursor_y, fx)) {
      cursor_x = fx;
      return;
    }
}
void find_down(void) {
  for (int fy = cursor_y + 1; fy < CANVAS_H; fy++)
    if (PX(fy, cursor_x)) {
      cursor_y = fy;
      return;
    }
  for (int fy = 0; fy < cursor_y; fy++)
    if (PX(fy, cursor_x)) {
      cursor_y = fy;
      return;
    }
}
void find_up(void) {
  for (int fy = cursor_y - 1; fy >= 0; fy--)
    if (PX(fy, cursor_x)) {
      cursor_y = fy;
      return;
    }
  for (int fy = CANVAS_H - 1; fy > cursor_y; fy--)
    if (PX(fy, cursor_x)) {
      cursor_y = fy;
      return;
    }
}

guint32 fill_color_at(int x, int y, guint32 fg) {
  switch (fill_pattern) {
  case FILL_CHECKER:
    return ((x + y) & 1) ? bg_color : fg;
  case FILL_HSTRIPES:
    return (y & 1) ? bg_color : fg;
  case FILL_VSTRIPES:
    return (x & 1) ? bg_color : fg;
  case FILL_HALFTONE: {
    static const int bayer[4][4] = {
        {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    return bayer[y & 3][x & 3] < 4 ? fg : bg_color;
  }
  default:
    return fg;
  }
}

void flood_fill(int sx, int sy, guint32 fill_color) {
  guint32 target = PX(sy, sx);
  if (fill_pattern == FILL_SOLID && target == fill_color)
    return;
  size_t total = (size_t)CANVAS_W * CANVAS_H;
  guint32 *before_snap = malloc(total * sizeof(guint32));
  int *queue = malloc(total * sizeof(int));
  gboolean *visited = calloc(total, sizeof(gboolean));
  if (!before_snap || !queue || !visited) {
    free(before_snap);
    free(queue);
    free(visited);
    return;
  }
  memcpy(before_snap, pixels, total * sizeof(guint32));
  int head = 0, tail = 0;
  visited[sy * CANVAS_W + sx] = TRUE;
  queue[tail++] = sy * CANVAS_W + sx;
  PX(sy, sx) = fill_color_at(sx, sy, fill_color);
  while (head < tail) {
    int pos = queue[head++];
    int x = pos % CANVAS_W, y = pos / CANVAS_W;
    int neighbors[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
    for (int i = 0; i < 4; i++) {
      int nx = neighbors[i][0], ny = neighbors[i][1];
      if (nx < 0 || nx >= CANVAS_W || ny < 0 || ny >= CANVAS_H)
        continue;
      int npos = ny * CANVAS_W + nx;
      if (visited[npos] || PX(ny, nx) != target)
        continue;
      visited[npos] = TRUE;
      PX(ny, nx) = fill_color_at(nx, ny, fill_color);
      queue[tail++] = npos;
    }
  }
  free(visited);
  free(queue);
  commit_canvas_snapshot(before_snap, CANVAS_W, CANVAS_H);
}

int snap_coord(int coord, gboolean horizontal) {
  if (!guide_snap || guide_count == 0)
    return coord;
  int best = coord, bestd = 3;
  for (int i = 0; i < guide_count; i++) {
    if (guides[i].horizontal != horizontal)
      continue;
    int d = guides[i].coord - coord;
    if (d < 0)
      d = -d;
    if (d < bestd) {
      bestd = d;
      best = guides[i].coord;
    }
  }
  return best;
}

void apply_gradient_linear(int x0, int y0, int x1, int y1, guint32 c1,
                           guint32 c2) {
  int r1 = (c1 >> 16) & 0xff, g1 = (c1 >> 8) & 0xff, b1 = c1 & 0xff;
  int r2 = (c2 >> 16) & 0xff, g2 = (c2 >> 8) & 0xff, b2 = c2 & 0xff;
  double dx = x1 - x0, dy = y1 - y0;
  double len2 = dx * dx + dy * dy;
  int rx0 = 0, ry0 = 0, rx1 = CANVAS_W - 1, ry1 = CANVAS_H - 1;
  if (visual_mode) {
    rx0 = MIN(cursor_x, visual_anchor_x);
    rx1 = MAX(cursor_x, visual_anchor_x);
    ry0 = MIN(cursor_y, visual_anchor_y);
    ry1 = MAX(cursor_y, visual_anchor_y);
  }
  begin_undo_action();
  for (int y = ry0; y <= ry1; y++) {
    for (int x = rx0; x <= rx1; x++) {
      double t = 0.0;
      if (len2 > 0.5)
        t = ((x - x0) * dx + (y - y0) * dy) / len2;
      t = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
      int ri = (int)(r1 + t * (r2 - r1) + 0.5);
      int gi = (int)(g1 + t * (g2 - g1) + 0.5);
      int bi = (int)(b1 + t * (b2 - b1) + 0.5);
      push_undo(x, y);
      PX(y, x) = PACK_RGBA(CLAMP(ri, 0, 255), CLAMP(gi, 0, 255),
                           CLAMP(bi, 0, 255), 255);
    }
  }
  commit_undo_action();
}
