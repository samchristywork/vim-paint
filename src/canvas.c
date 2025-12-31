#include "canvas.h"
#include "layers.h"
#include "main.h"
#include "palette.h"
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

/* Return the fill color for pixel (x,y) given current fill_pattern.
   fg is the primary color, bg_color is the secondary (transparent pixels
   in pattern positions use bg_color with alpha=0 if bg is transparent). */
guint32 fill_color_at(int x, int y, guint32 fg) {
  switch (fill_pattern) {
  case FILL_CHECKER:
    return ((x + y) & 1) ? bg_color : fg;
  case FILL_HSTRIPES:
    return (y & 1) ? bg_color : fg;
  case FILL_VSTRIPES:
    return (x & 1) ? bg_color : fg;
  case FILL_HALFTONE: {
    /* 4x4 ordered-dither Bayer matrix at ~50% density */
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
  /* visited prevents re-queuing when bg_color == target under pattern fills */
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

/* Snap coord to nearest guide on that axis; returns snapped value. */
int snap_coord(int coord, gboolean horizontal) {
  if (!guide_snap || guide_count == 0)
    return coord;
  int best = coord, bestd = 3; /* snap radius: 3 cells */
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

static void get_vis_rect(int *x0, int *y0, int *x1, int *y1) {
  *x0 = 0;
  *y0 = 0;
  *x1 = CANVAS_W - 1;
  *y1 = CANVAS_H - 1;
  if (visual_mode) {
    *x0 = MIN(cursor_x, visual_anchor_x);
    *x1 = MAX(cursor_x, visual_anchor_x);
    *y0 = MIN(cursor_y, visual_anchor_y);
    *y1 = MAX(cursor_y, visual_anchor_y);
  }
}

void exec_replace(const char *arg) {
  char from_s[64] = "", to_s[64] = "";
  if (sscanf(arg, "%63s %63s", from_s, to_s) != 2) {
    cmd_flash("Usage: :replace <from> <to>");
    return;
  }
  unsigned int from_rgb, to_rgb;
  if (!parse_color(from_s, &from_rgb) || !parse_color(to_s, &to_rgb))
    return;
  guint32 from_px = PACK_RGBA((from_rgb >> 16) & 0xff, (from_rgb >> 8) & 0xff,
                              from_rgb & 0xff, 255);
  guint32 to_px = PACK_RGBA((to_rgb >> 16) & 0xff, (to_rgb >> 8) & 0xff,
                            to_rgb & 0xff, 255);
  if (from_px == to_px) {
    cmd_flash("Colors are the same.");
    return;
  }
  begin_undo_action();
  for (int y = 0; y < CANVAS_H; y++)
    for (int x = 0; x < CANVAS_W; x++)
      if (PX(y, x) == from_px) {
        push_undo(x, y);
        PX(y, x) = to_px;
      }
  commit_undo_action();
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

void exec_gradient(const char *arg) {
  char c1s[64] = "", c2s[64] = "", dir[4] = "";
  if (sscanf(arg, "%63s %63s %3s", c1s, c2s, dir) != 3 ||
      (strcmp(dir, "h") != 0 && strcmp(dir, "v") != 0)) {
    cmd_flash("Usage: :gradient <color1> <color2> h|v");
    return;
  }
  unsigned int rgb1, rgb2;
  if (!parse_color(c1s, &rgb1) || !parse_color(c2s, &rgb2)) {
    cmd_flash("Unknown color.");
    return;
  }
  int horiz = (dir[0] == 'h');
  int x0, y0, x1, y1;
  get_vis_rect(&x0, &y0, &x1, &y1);
  int span = horiz ? (x1 - x0 + 1) : (y1 - y0 + 1);
  int r1i = (rgb1 >> 16) & 0xff, g1i = (rgb1 >> 8) & 0xff, b1i = rgb1 & 0xff;
  int r2i = (rgb2 >> 16) & 0xff, g2i = (rgb2 >> 8) & 0xff, b2i = rgb2 & 0xff;
  guint32 *pos_color = malloc(span * sizeof(guint32));
  if (!pos_color) {
    cmd_flash("Out of memory.");
    return;
  }
  for (int i = 0; i < span; i++) {
    double t = span > 1 ? i / (double)(span - 1) : 0.0;
    int ri = (int)(r1i + t * (r2i - r1i) + 0.5);
    int gi = (int)(g1i + t * (g2i - g1i) + 0.5);
    int bi = (int)(b1i + t * (b2i - b1i) + 0.5);
    pos_color[i] = PACK_RGBA(ri, gi, bi, 255);
  }
  begin_undo_action();
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      push_undo(x, y);
      PX(y, x) = pos_color[horiz ? (x - x0) : (y - y0)];
    }
  commit_undo_action();
  free(pos_color);
  visual_mode = FALSE;
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

void exec_gradtool(const char *arg) {
  unsigned int rgb1 = 0, rgb2 = 0;
  if (*arg) {
    char c1s[64] = "", c2s[64] = "";
    if (sscanf(arg, "%63s %63s", c1s, c2s) != 2 || !parse_color(c1s, &rgb1) ||
        !parse_color(c2s, &rgb2)) {
      cmd_flash("Usage: :gradtool [color1 color2]");
      return;
    }
  } else {
    rgb1 = ((fg_color >> 8) & 0xffffff);
    rgb2 = ((bg_color >> 8) & 0xffffff);
  }
  grad_c1 = rgb1;
  grad_c2 = rgb2;
  gradient_tool = TRUE;
  grad_dragging = FALSE;
  cmd_flash("Click and drag to apply gradient (Esc to cancel)");
}

void exec_brushdefine(const char *arg) {
  if (*arg) {
    memset(custom_brush_pixels, 0, sizeof(custom_brush_pixels));
    int row = 0, maxcol = 0;
    const char *p = arg;
#define CUSTOM_BRUSH_MAX 16
    while (*p && row < CUSTOM_BRUSH_MAX) {
      int col = 0;
      while (*p && *p != '/' && col < CUSTOM_BRUSH_MAX) {
        custom_brush_pixels[row][col] =
            (*p == '#' || *p == '*' || *p == '1') ? TRUE : FALSE;
        col++;
        p++;
      }
      if (col > maxcol)
        maxcol = col;
      row++;
      if (*p == '/')
        p++;
    }
#undef CUSTOM_BRUSH_MAX
    if (row == 0 || maxcol == 0) {
      cmd_flash("Empty pattern.");
      return;
    }
    custom_brush_w = maxcol;
    custom_brush_h = row;
  } else if (visual_mode) {
    int x0 = MIN(cursor_x, visual_anchor_x);
    int x1 = MAX(cursor_x, visual_anchor_x);
    int y0 = MIN(cursor_y, visual_anchor_y);
    int y1 = MAX(cursor_y, visual_anchor_y);
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    if (w > 16)
      w = 16;
    if (h > 16)
      h = 16;
    memset(custom_brush_pixels, 0, sizeof(custom_brush_pixels));
    for (int dy = 0; dy < h; dy++)
      for (int dx = 0; dx < w; dx++)
        custom_brush_pixels[dy][dx] = (PX(y0 + dy, x0 + dx) & 0xff) != 0;
    custom_brush_w = w;
    custom_brush_h = h;
    visual_mode = FALSE;
  } else {
    cmd_flash("Usage: :brushdefine <pattern>  or select region first");
    return;
  }
  brush_shape = 2;
  char msg[64];
  snprintf(msg, sizeof(msg), "Custom brush defined (%dx%d).", custom_brush_w,
           custom_brush_h);
  cmd_flash(msg);
}

void exec_hsl(const char *arg) {
  double delta = atof(arg);
  int is_hue = (cmd_buf[1] == 'h');
  int is_sat = (cmd_buf[1] == 's');
  int x0, y0, x1, y1;
  get_vis_rect(&x0, &y0, &x1, &y1);
  begin_undo_action();
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      guint32 px = PX(y, x);
      guchar a = px & 0xff;
      if (a == 0)
        continue;
      double r = ((px >> 24) & 0xff) / 255.0;
      double g = ((px >> 16) & 0xff) / 255.0;
      double b = ((px >> 8) & 0xff) / 255.0;
      double h, s, l;
      rgb_to_hsl(r, g, b, &h, &s, &l);
      if (is_hue)
        h = fmod(h + delta + 720.0, 360.0);
      else if (is_sat)
        s = CLAMP(s + delta / 100.0, 0.0, 1.0);
      else
        l = CLAMP(l + delta / 100.0, 0.0, 1.0);
      hsl_to_rgb(h, s, l, &r, &g, &b);
      push_undo(x, y);
      PX(y, x) = PACK_RGBA((int)(r * 255 + 0.5), (int)(g * 255 + 0.5),
                           (int)(b * 255 + 0.5), a);
    }
  commit_undo_action();
  visual_mode = FALSE;
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

void exec_dither(const char *arg) {
  char c1s[64] = "", c2s[64] = "", pat[16] = "";
  if (sscanf(arg, "%63s %63s %15s", c1s, c2s, pat) != 3 ||
      (strcmp(pat, "ordered") != 0 && strcmp(pat, "fs") != 0)) {
    cmd_flash("Usage: :dither <color1> <color2> ordered|fs");
    return;
  }
  unsigned int rgb1, rgb2;
  if (!parse_color(c1s, &rgb1) || !parse_color(c2s, &rgb2)) {
    cmd_flash("Unknown color.");
    return;
  }
  int r1 = (rgb1 >> 16) & 0xff, g1 = (rgb1 >> 8) & 0xff, b1 = rgb1 & 0xff;
  int r2 = (rgb2 >> 16) & 0xff, g2 = (rgb2 >> 8) & 0xff, b2 = rgb2 & 0xff;
  guint32 px1 = PACK_RGBA(r1, g1, b1, 255);
  guint32 px2 = PACK_RGBA(r2, g2, b2, 255);
  int x0, y0, x1, y1;
  get_vis_rect(&x0, &y0, &x1, &y1);
  guint32 *before_snap =
      malloc((size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
  if (!before_snap) {
    cmd_flash("Out of memory.");
    return;
  }
  memcpy(before_snap, pixels, (size_t)CANVAS_W * CANVAS_H * sizeof(guint32));

  if (strcmp(pat, "ordered") == 0) {
    static const int bayer[4][4] = {
        {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) {
        guint32 px = PX(y, x);
        if ((px & 0xff) == 0)
          continue;
        int pr = (px >> 24) & 0xff, pg = (px >> 16) & 0xff,
            pb = (px >> 8) & 0xff;
        int dr1 = pr - r1, dg1 = pg - g1, db1 = pb - b1;
        int dr2 = pr - r2, dg2 = pg - g2, db2 = pb - b2;
        double d1 = dr1 * dr1 + dg1 * dg1 + db1 * db1;
        double d2 = dr2 * dr2 + dg2 * dg2 + db2 * db2;
        double t = (d1 + d2 > 0) ? d1 / (d1 + d2) : 0.5;
        PX(y, x) = (t >= (bayer[y % 4][x % 4] + 0.5) / 16.0) ? px2 : px1;
      }
  } else {
    int W = x1 - x0 + 1, H = y1 - y0 + 1;
    double *er = calloc((size_t)W * H, sizeof(double));
    double *eg = calloc((size_t)W * H, sizeof(double));
    double *eb = calloc((size_t)W * H, sizeof(double));
    if (!er || !eg || !eb) {
      free(er);
      free(eg);
      free(eb);
      free(before_snap);
      cmd_flash("Out of memory.");
      return;
    }
    for (int y = y0; y <= y1; y++) {
      for (int x = x0; x <= x1; x++) {
        guint32 px = PX(y, x);
        if ((px & 0xff) == 0)
          continue;
        int ey = y - y0, ex = x - x0, idx = ey * W + ex;
        double nr = ((px >> 24) & 0xff) + er[idx];
        double ng = ((px >> 16) & 0xff) + eg[idx];
        double nb = ((px >> 8) & 0xff) + eb[idx];
        int dr1 = (int)nr - r1, dg1 = (int)ng - g1, db1 = (int)nb - b1;
        int dr2 = (int)nr - r2, dg2 = (int)ng - g2, db2 = (int)nb - b2;
        guint32 chosen;
        int cr, cg, cb;
        if (dr1 * dr1 + dg1 * dg1 + db1 * db1 <=
            dr2 * dr2 + dg2 * dg2 + db2 * db2) {
          chosen = px1;
          cr = r1;
          cg = g1;
          cb = b1;
        } else {
          chosen = px2;
          cr = r2;
          cg = g2;
          cb = b2;
        }
        PX(y, x) = chosen;
        double qr = nr - cr, qg = ng - cg, qb = nb - cb;
#define SPREAD(dy, dx, w)                                                      \
  do {                                                                         \
    int ny = ey + (dy), nx = ex + (dx);                                        \
    if (ny >= 0 && ny < H && nx >= 0 && nx < W) {                              \
      int ni = ny * W + nx;                                                    \
      er[ni] += qr * (w);                                                      \
      eg[ni] += qg * (w);                                                      \
      eb[ni] += qb * (w);                                                      \
    }                                                                          \
  } while (0)
        SPREAD(0, 1, 7.0 / 16);
        SPREAD(1, -1, 3.0 / 16);
        SPREAD(1, 0, 5.0 / 16);
        SPREAD(1, 1, 1.0 / 16);
#undef SPREAD
      }
    }
    free(er);
    free(eg);
    free(eb);
  }
  commit_canvas_snapshot(before_snap, CANVAS_W, CANVAS_H);
  visual_mode = FALSE;
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

void exec_find_color(const char *arg) {
  (void)arg;
  const char *val = cmd_buf + 12;
  unsigned int rgb;
  if (!parse_color(val, &rgb)) {
    cmd_flash("Unknown color.");
    return;
  }
  int tr = (rgb >> 16) & 0xff;
  int tg = (rgb >> 8) & 0xff;
  int tb = rgb & 0xff;
  int found_x = -1, found_y = -1, found_dist = INT_MAX;
  double found_color_dist = 1e9;
  for (int y = 0; y < CANVAS_H; y++) {
    for (int x = 0; x < CANVAS_W; x++) {
      guint32 px = PX(y, x);
      int pr = (px >> 24) & 0xff;
      int pg = (px >> 16) & 0xff;
      int pb = (px >> 8) & 0xff;
      double dr = (pr - tr) / 255.0, dg = (pg - tg) / 255.0,
             db = (pb - tb) / 255.0;
      double cdist = dr * dr + dg * dg + db * db;
      int mdist = abs(x - cursor_x) + abs(y - cursor_y);
      if (cdist < found_color_dist ||
          (cdist == found_color_dist && mdist < found_dist)) {
        found_color_dist = cdist;
        found_dist = mdist;
        found_x = x;
        found_y = y;
      }
    }
  }
  if (found_x < 0) {
    cmd_flash("Color not found on canvas.");
  } else {
    cursor_x = found_x;
    cursor_y = found_y;
    status_update();
    gtk_widget_queue_draw(main_canvas);
  }
  cmd_set("");
}

void exec_goto(const char *arg) {
  int gx = 0, gy = 0;
  if (sscanf(arg, "%d,%d", &gx, &gy) != 2)
    sscanf(arg, "%d %d", &gx, &gy);
  if (gx < 1 || gy < 1) {
    cmd_flash("Usage: :goto col,row  (1-based)");
    return;
  }
  cursor_x = CLAMP(gx - 1, 0, CANVAS_W - 1);
  cursor_y = CLAMP(gy - 1, 0, CANVAS_H - 1);
  status_update();
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

void exec_scale(const char *arg) {
  int n = atoi(arg);
  if (n < 2 || n > 8) {
    cmd_flash("Usage: :scale N  (N = 2..8, requires visual selection)");
    return;
  }
  if (!visual_mode) {
    cmd_flash(":scale requires a visual selection.");
    return;
  }
  int x0 = MIN(cursor_x, visual_anchor_x);
  int x1 = MAX(cursor_x, visual_anchor_x);
  int y0 = MIN(cursor_y, visual_anchor_y);
  int y1 = MAX(cursor_y, visual_anchor_y);
  int W = x1 - x0 + 1, H = y1 - y0 + 1;
  guint32 *tmp = malloc((size_t)W * H * sizeof(guint32));
  if (!tmp) {
    cmd_flash("Out of memory.");
    return;
  }
  for (int sy = 0; sy < H; sy++)
    for (int sx = 0; sx < W; sx++)
      tmp[sy * W + sx] = PX(y0 + sy, x0 + sx);
  begin_undo_action();
  for (int sy = 0; sy < H; sy++)
    for (int sx = 0; sx < W; sx++) {
      guint32 col = tmp[sy * W + sx];
      for (int dy = 0; dy < n; dy++)
        for (int dx = 0; dx < n; dx++) {
          int px = x0 + sx * n + dx;
          int py = y0 + sy * n + dy;
          if (px < CANVAS_W && py < CANVAS_H)
            paint_pixel(px, py, col);
        }
    }
  free(tmp);
  commit_undo_action();
  visual_mode = FALSE;
  status_update();
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

void exec_fliph(const char *arg) {
  (void)arg;
  begin_undo_action();
  for (int y = 0; y < CANVAS_H; y++)
    for (int x = 0; x < CANVAS_W / 2; x++) {
      push_undo(x, y);
      push_undo(CANVAS_W - 1 - x, y);
    }
  for (int y = 0; y < CANVAS_H; y++)
    for (int x = 0; x < CANVAS_W / 2; x++) {
      guint32 tmp = PX(y, x);
      PX(y, x) = PX(y, CANVAS_W - 1 - x);
      PX(y, CANVAS_W - 1 - x) = tmp;
    }
  commit_undo_action();
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

void exec_flipv(const char *arg) {
  (void)arg;
  begin_undo_action();
  for (int y = 0; y < CANVAS_H / 2; y++)
    for (int x = 0; x < CANVAS_W; x++) {
      push_undo(x, y);
      push_undo(x, CANVAS_H - 1 - y);
    }
  for (int y = 0; y < CANVAS_H / 2; y++)
    for (int x = 0; x < CANVAS_W; x++) {
      guint32 tmp = PX(y, x);
      PX(y, x) = PX(CANVAS_H - 1 - y, x);
      PX(CANVAS_H - 1 - y, x) = tmp;
    }
  commit_undo_action();
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

void exec_invert(const char *arg) {
  (void)arg;
  int x0, y0, x1, y1;
  get_vis_rect(&x0, &y0, &x1, &y1);
  begin_undo_action();
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      guint32 px = PX(y, x);
      guchar a = px & 0xff;
      if (a == 0)
        continue;
      guchar r = ~((px >> 24) & 0xff);
      guchar g = ~((px >> 16) & 0xff);
      guchar b = ~((px >> 8) & 0xff);
      push_undo(x, y);
      PX(y, x) = PACK_RGBA(r, g, b, a);
    }
  commit_undo_action();
  visual_mode = FALSE;
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

void exec_blur(const char *arg) {
  int radius = 1;
  if (*arg) {
    radius = atoi(arg);
    if (radius < 1 || radius > 64) {
      cmd_flash("Usage: :blur [N]  (N = 1..64)");
      return;
    }
  }
  int x0, y0, x1, y1;
  get_vis_rect(&x0, &y0, &x1, &y1);
  int w = x1 - x0 + 1, h = y1 - y0 + 1;
  guint32 *tmp = malloc((size_t)w * h * sizeof(guint32));
  if (!tmp) {
    cmd_flash("Out of memory.");
    return;
  }
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      long sr = 0, sg = 0, sb = 0, sa = 0, cnt = 0;
      for (int dx = -radius; dx <= radius; dx++) {
        int sx = CLAMP(x0 + x + dx, x0, x1);
        guint32 px = PX(y0 + y, sx);
        sr += (px >> 24) & 0xff;
        sg += (px >> 16) & 0xff;
        sb += (px >> 8) & 0xff;
        sa += px & 0xff;
        cnt++;
      }
      tmp[y * w + x] = PACK_RGBA(sr / cnt, sg / cnt, sb / cnt, sa / cnt);
    }
  }
  begin_undo_action();
  for (int x = 0; x < w; x++) {
    for (int y = 0; y < h; y++) {
      long sr = 0, sg = 0, sb = 0, sa = 0, cnt = 0;
      for (int dy = -radius; dy <= radius; dy++) {
        int sy = CLAMP(y + dy, 0, h - 1);
        guint32 px = tmp[sy * w + x];
        sr += (px >> 24) & 0xff;
        sg += (px >> 16) & 0xff;
        sb += (px >> 8) & 0xff;
        sa += px & 0xff;
        cnt++;
      }
      push_undo(x0 + x, y0 + y);
      PX(y0 + y, x0 + x) = PACK_RGBA(sr / cnt, sg / cnt, sb / cnt, sa / cnt);
    }
  }
  free(tmp);
  commit_undo_action();
  visual_mode = FALSE;
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

void exec_stroke(const char *arg) {
  (void)arg;
  if (!visual_mode) {
    cmd_flash(":stroke requires a visual selection.");
    return;
  }
  int x0 = MIN(cursor_x, visual_anchor_x);
  int x1 = MAX(cursor_x, visual_anchor_x);
  int y0 = MIN(cursor_y, visual_anchor_y);
  int y1 = MAX(cursor_y, visual_anchor_y);
  begin_undo_action();
  for (int x = x0; x <= x1; x++) {
    paint_brush(x, y0, fg_color);
    paint_brush(x, y1, fg_color);
  }
  for (int y = y0 + 1; y < y1; y++) {
    paint_brush(x0, y, fg_color);
    paint_brush(x1, y, fg_color);
  }
  commit_undo_action();
  visual_mode = FALSE;
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

void exec_rotate(const char *arg) {
  (void)arg;
  if (layer_count > 1)
    layers_flatten();
  int bw = CANVAS_W, bh = CANVAS_H;
  guint32 *before_snap = malloc((size_t)bw * bh * sizeof(guint32));
  if (!before_snap) {
    cmd_flash("Out of memory.");
    return;
  }
  memcpy(before_snap, pixels, (size_t)bw * bh * sizeof(guint32));
  int nW = CANVAS_H, nH = CANVAS_W;
  guint32 *np = malloc((size_t)nW * nH * sizeof(guint32));
  if (!np) {
    free(before_snap);
    cmd_flash("Out of memory.");
    return;
  }
  for (int y = 0; y < CANVAS_H; y++)
    for (int x = 0; x < CANVAS_W; x++)
      np[x * nW + (CANVAS_H - 1 - y)] = PX(y, x);
  free(pixels);
  pixels = np;
  layer_bufs[layer_active] = pixels;
  CANVAS_W = nW;
  CANVAS_H = nH;
  cursor_x = CLAMP(cursor_x, 0, CANVAS_W - 1);
  cursor_y = CLAMP(cursor_y, 0, CANVAS_H - 1);
  commit_canvas_snapshot(before_snap, bw, bh);
  zoom_resize();
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

void exec_resize(const char *arg) {
  int nw = 0, nh = 0;
  if (sscanf(arg, "%dx%d", &nw, &nh) != 2)
    sscanf(arg, "%d %d", &nw, &nh);
  if (nw < 1 || nh < 1 || nw > 16384 || nh > 16384) {
    cmd_flash("Usage: :resize WxH  (max 16384)");
    return;
  }
  if (layer_count > 1)
    layers_flatten();
  guint32 *before_snap =
      malloc((size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
  guint32 *np = calloc((size_t)nw * nh, sizeof(guint32));
  if (!before_snap || !np) {
    free(before_snap);
    free(np);
    cmd_flash("Out of memory.");
    return;
  }
  memcpy(before_snap, pixels, (size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
  int bw = CANVAS_W, bh = CANVAS_H;
  int cw = MIN(CANVAS_W, nw), ch = MIN(CANVAS_H, nh);
  for (int y = 0; y < ch; y++)
    for (int x = 0; x < cw; x++)
      np[y * nw + x] = PX(y, x);
  free(pixels);
  pixels = np;
  layer_bufs[layer_active] = pixels;
  CANVAS_W = nw;
  CANVAS_H = nh;
  cursor_x = CLAMP(cursor_x, 0, CANVAS_W - 1);
  cursor_y = CLAMP(cursor_y, 0, CANVAS_H - 1);
  visual_anchor_x = CLAMP(visual_anchor_x, 0, CANVAS_W - 1);
  visual_anchor_y = CLAMP(visual_anchor_y, 0, CANVAS_H - 1);
  commit_canvas_snapshot(before_snap, bw, bh);
  zoom_resize();
  gtk_widget_queue_draw(main_canvas);
  if ((size_t)nw * nh > (size_t)4096 * 4096) {
    char wmsg[64];
    snprintf(wmsg, sizeof(wmsg), "Large canvas: ~%zu MB/layer",
             (size_t)nw * nh * 4 / (1024 * 1024));
    cmd_flash(wmsg);
  } else {
    cmd_set("");
  }
}

void exec_center(const char *arg) {
  (void)arg;
  int min_x = CANVAS_W, max_x = -1, min_y = CANVAS_H, max_y = -1;
  for (int y = 0; y < CANVAS_H; y++)
    for (int x = 0; x < CANVAS_W; x++)
      if (PX(y, x)) {
        if (x < min_x)
          min_x = x;
        if (x > max_x)
          max_x = x;
        if (y < min_y)
          min_y = y;
        if (y > max_y)
          max_y = y;
      }
  if (max_x < 0) {
    cmd_flash("Nothing to center.");
    return;
  }
  int dx = (CANVAS_W - (max_x - min_x + 1)) / 2 - min_x;
  int dy = (CANVAS_H - (max_y - min_y + 1)) / 2 - min_y;
  if (dx == 0 && dy == 0) {
    cmd_flash("Already centered.");
    return;
  }
  guint32 *before_snap =
      malloc((size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
  guint32 *np = calloc((size_t)CANVAS_W * CANVAS_H, sizeof(guint32));
  if (!before_snap || !np) {
    free(before_snap);
    free(np);
    cmd_flash("Out of memory.");
    return;
  }
  memcpy(before_snap, pixels, (size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
  int bw = CANVAS_W, bh = CANVAS_H;
  for (int y = 0; y < CANVAS_H; y++)
    for (int x = 0; x < CANVAS_W; x++) {
      if (!PX(y, x))
        continue;
      int nx = x + dx, ny = y + dy;
      if (nx >= 0 && nx < CANVAS_W && ny >= 0 && ny < CANVAS_H)
        np[ny * CANVAS_W + nx] = PX(y, x);
    }
  memcpy(pixels, np, (size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
  free(np);
  commit_canvas_snapshot(before_snap, bw, bh);
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

void exec_crop(const char *arg) {
  (void)arg;
  if (layer_count > 1)
    layers_flatten();
  int min_x = CANVAS_W, max_x = -1, min_y = CANVAS_H, max_y = -1;
  for (int y = 0; y < CANVAS_H; y++)
    for (int x = 0; x < CANVAS_W; x++)
      if (PX(y, x)) {
        if (x < min_x)
          min_x = x;
        if (x > max_x)
          max_x = x;
        if (y < min_y)
          min_y = y;
        if (y > max_y)
          max_y = y;
      }
  if (max_x < 0) {
    cmd_flash("Nothing to crop.");
    return;
  }
  if (min_x == 0 && min_y == 0 && max_x == CANVAS_W - 1 &&
      max_y == CANVAS_H - 1) {
    cmd_flash("Already at content bounds.");
    return;
  }
  int nW = max_x - min_x + 1, nH = max_y - min_y + 1;
  guint32 *before_snap =
      malloc((size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
  guint32 *np = calloc((size_t)nW * nH, sizeof(guint32));
  if (!before_snap || !np) {
    free(before_snap);
    free(np);
    cmd_flash("Out of memory.");
    return;
  }
  memcpy(before_snap, pixels, (size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
  int bw = CANVAS_W, bh = CANVAS_H;
  for (int y = min_y; y <= max_y; y++)
    for (int x = min_x; x <= max_x; x++)
      np[(y - min_y) * nW + (x - min_x)] = PX(y, x);
  free(pixels);
  pixels = np;
  layer_bufs[layer_active] = pixels;
  CANVAS_W = nW;
  CANVAS_H = nH;
  cursor_x = CLAMP(cursor_x - min_x, 0, CANVAS_W - 1);
  cursor_y = CLAMP(cursor_y - min_y, 0, CANVAS_H - 1);
  commit_canvas_snapshot(before_snap, bw, bh);
  zoom_resize();
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

void exec_text(const char *arg) {
  cairo_surface_t *measure_surf =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  cairo_t *mcr = cairo_create(measure_surf);
  cairo_select_font_face(mcr, text_font_family, CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(mcr, text_font_size);
  cairo_text_extents_t te;
  cairo_text_extents(mcr, arg, &te);
  cairo_font_extents_t fe_m;
  cairo_font_extents(mcr, &fe_m);
  cairo_destroy(mcr);
  cairo_surface_destroy(measure_surf);
  int sw = (int)(te.x_bearing + te.width + 2);
  int sh = (int)(fe_m.ascent + fe_m.descent + 2);
  if (sw < 1)
    sw = 1;
  if (sh < 1)
    sh = 1;
  cairo_surface_t *tsurf =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
  cairo_t *tcr = cairo_create(tsurf);
  cairo_font_options_t *fo = cairo_font_options_create();
  cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_NONE);
  cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_FULL);
  cairo_set_font_options(tcr, fo);
  cairo_font_options_destroy(fo);
  cairo_set_source_rgb(tcr, 1, 1, 1);
  cairo_paint(tcr);
  cairo_select_font_face(tcr, text_font_family, CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(tcr, text_font_size);
  cairo_font_extents_t fe;
  cairo_font_extents(tcr, &fe);
  cairo_set_source_rgb(tcr, 0, 0, 0);
  cairo_move_to(tcr, 0, fe.ascent);
  cairo_show_text(tcr, arg);
  cairo_surface_flush(tsurf);
  unsigned char *tdata = cairo_image_surface_get_data(tsurf);
  int tstride = cairo_image_surface_get_stride(tsurf);
  begin_undo_action();
  for (int ty = 0; ty < sh; ty++)
    for (int tx = 0; tx < sw; tx++) {
      unsigned char tr = tdata[ty * tstride + tx * 4 + 2];
      if (tr > 128)
        continue;
      int tcx = cursor_x + tx, tcy = cursor_y + ty;
      if (tcx < 0 || tcx >= CANVAS_W || tcy < 0 || tcy >= CANVAS_H)
        continue;
      push_undo(tcx, tcy);
      PX(tcy, tcx) = fg_color;
    }
  commit_undo_action();
  cairo_destroy(tcr);
  cairo_surface_destroy(tsurf);
  gtk_widget_queue_draw(main_canvas);
  char tmsg[320];
  snprintf(tmsg, sizeof(tmsg), "Text [%s %.4gpt]", text_font_family,
           text_font_size);
  cmd_flash(tmsg);
}
