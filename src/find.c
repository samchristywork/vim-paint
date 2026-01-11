#include "find.h"
#include "effects.h"
#include "main.h"

void find_right(void) {
  for (int fx = cursor_x + 1; fx < CANVAS_W; fx++)
    if (PX(cursor_y, fx)) { cursor_x = fx; return; }
  for (int fx = 0; fx < cursor_x; fx++)
    if (PX(cursor_y, fx)) { cursor_x = fx; return; }
}

void find_left(void) {
  for (int fx = cursor_x - 1; fx >= 0; fx--)
    if (PX(cursor_y, fx)) { cursor_x = fx; return; }
  for (int fx = CANVAS_W - 1; fx > cursor_x; fx--)
    if (PX(cursor_y, fx)) { cursor_x = fx; return; }
}

void find_down(void) {
  for (int fy = cursor_y + 1; fy < CANVAS_H; fy++)
    if (PX(fy, cursor_x)) { cursor_y = fy; return; }
  for (int fy = 0; fy < cursor_y; fy++)
    if (PX(fy, cursor_x)) { cursor_y = fy; return; }
}

void find_up(void) {
  for (int fy = cursor_y - 1; fy >= 0; fy--)
    if (PX(fy, cursor_x)) { cursor_y = fy; return; }
  for (int fy = CANVAS_H - 1; fy > cursor_y; fy--)
    if (PX(fy, cursor_x)) { cursor_y = fy; return; }
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

void find_color(const char *arg) {
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

gboolean exec_find(const char *cmd, const char *arg) {
  if (strncmp(cmd, ":find color ", 12) == 0) { find_color(arg); return TRUE; }
  return FALSE;
}
