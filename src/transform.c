#include "transform.h"
#include "draw.h"
#include "layers.h"
#include "main.h"
#include "palette.h"
#include "undo.h"

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
