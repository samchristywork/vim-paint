#ifndef VIMPAINT_CANVAS_H
#define VIMPAINT_CANVAS_H

#include "state.h"

void insert_paint(void);
void draw_line(int x0, int y0, int x1, int y1, guint32 color);
void paint_pixel_raw(int x, int y, guint32 color);
void paint_pixel(int x, int y, guint32 color);
void paint_brush(int x, int y, guint32 color);
void find_right(void);
void find_left(void);
void find_down(void);
void find_up(void);
guint32 fill_color_at(int x, int y, guint32 fg);
void flood_fill(int sx, int sy, guint32 fill_color);
int snap_coord(int coord, gboolean horizontal);
void apply_gradient_linear(int x0, int y0, int x1, int y1, guint32 c1, guint32 c2);
void exec_replace(const char *arg);
void exec_gradient(const char *arg);
void exec_gradtool(const char *arg);
void exec_brushdefine(const char *arg);
void exec_hsl(const char *arg);
void exec_dither(const char *arg);
void exec_find_color(const char *arg);
void exec_goto(const char *arg);
void exec_scale(const char *arg);
void exec_fliph(const char *arg);
void exec_flipv(const char *arg);
void exec_invert(const char *arg);
void exec_blur(const char *arg);
void exec_stroke(const char *arg);
void exec_rotate(const char *arg);
void exec_resize(const char *arg);
void exec_center(const char *arg);
void exec_crop(const char *arg);
void exec_text(const char *arg);

#endif
