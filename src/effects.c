#include "effects.h"
#include "main.h"
#include "palette.h"
#include "undo.h"

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

static void replace(const char *arg) {
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

static void gradient(const char *arg) {
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

static void gradtool(const char *arg) {
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

static void brushdefine(const char *arg) {
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

static void hsl(const char *arg) {
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

static void dither(const char *arg) {
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

static void invert(const char *arg) {
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

static void blur(const char *arg) {
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

gboolean exec_effects(const char *cmd, const char *arg) {
  if (strncmp(cmd, ":replace ", 9) == 0 && *arg)   { replace(arg);   return TRUE; }
  if (strncmp(cmd, ":gradient ", 10) == 0 && *arg) { gradient(arg);  return TRUE; }
  if (strncmp(cmd, ":gradtool", 9) == 0)           { gradtool(arg);  return TRUE; }
  if (strcmp(cmd, ":brushdefine") == 0 ||
      strncmp(cmd, ":brushdefine ", 13) == 0)       { brushdefine(arg); return TRUE; }
  if (strncmp(cmd, ":hue ", 5) == 0 ||
      strncmp(cmd, ":sat ", 5) == 0 ||
      strncmp(cmd, ":bright ", 8) == 0)             { hsl(arg);      return TRUE; }
  if (strncmp(cmd, ":dither ", 8) == 0 && *arg)    { dither(arg);   return TRUE; }
  if (strcmp(cmd, ":invert") == 0)                  { invert(arg);   return TRUE; }
  if (strcmp(cmd, ":blur") == 0 ||
      strncmp(cmd, ":blur ", 6) == 0)               { blur(arg);     return TRUE; }
  return FALSE;
}
